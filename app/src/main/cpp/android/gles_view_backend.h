#pragma once

namespace dmcviewer {

// Registers the OpenGL ES 3 view backend (view_gpu.h) for render_view. The
// EGL context is created on the first frame; until then, and on any device
// without OpenGL ES 3, frames stay on the software rasteriser.
void install_gles_view_backend();

}  // namespace dmcviewer
