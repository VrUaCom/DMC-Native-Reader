#pragma once

#include "dmcresource/mesh.h"

namespace dmcresource {

struct ViewState {
    float yaw_radians{0.65f};
    float pitch_radians{-0.45f};
    float zoom{1.0f};
    bool wireframe{false};
};

RgbaImage render_view(const Mesh& mesh, int width, int height, const ViewState& view);

}  // namespace dmcresource
