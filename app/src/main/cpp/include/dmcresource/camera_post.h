#pragma once

#include <array>
#include <cstdint>

// The photo camera's development as numbers for a GPU backend: the core
// (spider/camera.cpp) decides every value, the backend's shaders only
// evaluate them on the frame it drew, as they do for the fog. A frame the
// backend cannot develop is developed on the CPU instead (applied stays false).
namespace dmcresource {

struct CameraPost final {
    bool enabled{};

    // Optics, in front of the sensor.
    bool polarizer{};
    float polarizer_strength{};
    float polarizer_ring{};  // radians
    bool depth_of_field{};
    // Focus distance in millimetres: > 0 that distance, 0 the auto focus
    // (the middle of the frame), < 0 infinity.
    float focus_mm{};
    float focal_mm{50.0F};
    float coc_k{};           // f^2 / N (mm^2)
    float unit_mm{10.0F};    // a scene unit
    float max_radius{};      // circle of confusion cap, pixels
    float px_per_mm{};       // the frame's short side over a full frame's 24 mm
    float auto_focus_half{}; // half the side of the middle the auto focus reads, pixels
    int taps{};              // golden-angle points, already shaped by the blades
    std::array<float, 256> tap_xy{};
    bool bright_bokeh{};
    float distortion_k{};
    float fringe{};

    // The look: per-channel curves (exposure, white balance, highlights,
    // contrast, lift) on 8-bit values, then saturation, tint (or the cinema
    // grade), vignetting, sensor noise and sharpening.
    std::array<std::uint8_t, 768> curves{};  // red[256], green[256], blue[256]
    float saturation{};
    std::array<float, 3> tint{1.0F, 1.0F, 1.0F};
    bool cinema{};
    float vignette{};
    float noise_sigma{};
    int grain{1};
    bool chroma_noise{};
    std::uint32_t seed{};
    float sharpen{};

    // What the backend tells back.
    mutable bool applied{};
    mutable float focus_found{-1.0F};  // scene units the auto focus found; 0 infinity
};

}  // namespace dmcresource
