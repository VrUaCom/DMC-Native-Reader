#pragma once

#include <optional>

#include "dmcresource/image_preview.h"

namespace dmc::rengine::formats::mot {
struct Document;
}

// Stand-alone MOT view: a MOT carries only per-node channel curves (its node
// hierarchy, rest pose and mesh live in the MOD), so a lone MOT is drawn as
// its curves. Three panels (rotation / translation / scale) plot every
// animated channel over [0, end frame], sampled with the same evaluators the
// playback uses (compression 3: rengine 0x1402E9170 port, compression 2: its
// linear twin); one colour per node, lighter shades for y and z.
namespace dmcresource::motion {

[[nodiscard]] std::optional<ImagePreview> render_motion_chart(
    const dmc::rengine::formats::mot::Document& document, int width, int height);

}  // namespace dmcresource::motion
