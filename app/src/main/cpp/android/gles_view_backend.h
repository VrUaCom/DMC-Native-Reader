#pragma once

#ifndef __ANDROID__
#include <cstdint>
#include <vector>
#endif

namespace dmcviewer {

#ifndef __ANDROID__
// Off the phone (a desktop probe): what GpuViewBackend::set_window takes in
// place of a window; a pbuffer of this size stands in for it, and every
// frame shown on it is read into `pixels` (GL rows, bottom up).
struct PresentStandIn final {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels;
    int shown{};
};
#endif

// Registers the OpenGL ES 3 view backend (view_gpu.h) for render_view. The
// EGL context is created on the first frame; until then, and on any device
// without OpenGL ES 3, frames stay on the software rasteriser.
void install_gles_view_backend();

}  // namespace dmcviewer
