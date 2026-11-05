#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Viewer benchmark: the largest frame rate the view reaches with the current
// settings. Frames are drawn back to back (no frame pacing, no screen); each
// frame is what the viewer's render thread does — pose the motion when one
// plays, render (GPU or CPU, view_gpu.h) and copy the pixels out. The camera
// turns once per 240 frames so every side of the model and the room is drawn.
namespace dmcresource {

struct ViewBenchmarkResult final {
    int frames{};             // timed frames (warm-up frames excluded)
    int warmup_frames{};
    double seconds{};         // timed wall time
    double average_ms{};
    double best_ms{};
    double worst_ms{};
    double median_ms{};
    double low1_ms{};         // 99th percentile frame time ("1% low")
    std::uint64_t gpu_frames{};
    std::uint64_t cpu_frames{};
    std::uint64_t gpu_failures{};
    bool stopped{};           // a frame failed; the result covers the frames before it

    [[nodiscard]] double average_fps() const noexcept { return average_ms > 0.0 ? 1000.0 / average_ms : 0.0; }
    [[nodiscard]] double low1_fps() const noexcept { return low1_ms > 0.0 ? 1000.0 / low1_ms : 0.0; }
    [[nodiscard]] double best_fps() const noexcept { return best_ms > 0.0 ? 1000.0 / best_ms : 0.0; }
    [[nodiscard]] double worst_fps() const noexcept { return worst_ms > 0.0 ? 1000.0 / worst_ms : 0.0; }
};

struct ViewBenchmarkOptions final {
    double seconds{10.0};   // timed duration
    int max_frames{100000};
    int warmup_frames{5};   // first GPU frames upload textures and the room
};

// Draws one frame `index` (0-based, warm-up included); false stops the run.
using ViewBenchmarkFrame = std::function<bool(int index)>;

[[nodiscard]] ViewBenchmarkResult run_view_benchmark(const ViewBenchmarkOptions& options,
                                                     const ViewBenchmarkFrame& frame);

// Camera yaw of frame `index`: one full turn per 240 frames from `start`.
[[nodiscard]] float benchmark_yaw(float start, int index) noexcept;

// Statistics of a list of frame times (ms), as run_view_benchmark reports them.
[[nodiscard]] ViewBenchmarkResult summarize_frame_times(std::vector<double> frame_ms);

// Several lines for the viewer: renderer, resolution, settings, the numbers.
[[nodiscard]] std::string format_view_benchmark(const ViewBenchmarkResult& result, const std::string& renderer,
                                                int width, int height, const std::string& settings);

}  // namespace dmcresource
