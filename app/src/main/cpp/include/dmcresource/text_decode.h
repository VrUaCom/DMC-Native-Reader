#pragma once

#include <cstddef>
#include <cstdint>

#include "dmcresource/decode.h"

namespace dmcresource {

// Decodes a DMC3 stage text resource (`.txt`).
//
// Structural authority: dmc-rengine-cpp `src/formats/stage_txt.cpp` — a
// tokenized configuration text with `#SET` directives, `DOOR` / `BOXIN` /
// `NEXTROOM` keywords, stage-set values, `//` and block comments, quoted
// strings and numbers. NUL bytes disqualify the resource as text.
DecodeResult decode_stage_txt(const std::uint8_t* bytes, std::size_t size) noexcept;

// Decodes a DMC3 `.index` extraction/naming manifest.
//
// Structural authority: dmc-rengine-cpp `docs/formats/pnst-readonly-parser.md`
// and `LooseContainerListPolicy`. A line-based text manifest that may open with
// a magic directive line (the real corpus uses the literal line `PNST`), where
// `/` starts a comment, blank lines are skipped and `dummy` marks a declared
// sparse slot.
//
// The leading `PNST` is textual metadata: it must never be read as binary PNST
// container magic.
DecodeResult decode_index(const std::uint8_t* bytes, std::size_t size) noexcept;

}  // namespace dmcresource
