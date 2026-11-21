// Viewer benchmark (view_benchmark.h): frame-time statistics, warm-up,
// time / frame limits, early stop, the camera sweep and the report.
#include <cassert>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include "dmcresource/view_benchmark.h"

using namespace dmcresource;

int main() {
    // Statistics: 100 frames, 99 of 10 ms and one of 50 ms.
    {
        std::vector<double> ms(99U, 10.0);
        ms.push_back(50.0);
        const auto r = summarize_frame_times(ms);
        assert(r.frames == 100);
        assert(std::fabs(r.seconds - 1.04) < 1.0e-9);
        assert(std::fabs(r.average_ms - 10.4) < 1.0e-9);
        assert(r.best_ms == 10.0 && r.worst_ms == 50.0);
        assert(r.median_ms == 10.0);
        assert(r.low1_ms == 10.0);  // 99th percentile of 100 frames: the 99th
        assert(std::fabs(r.average_fps() - 1000.0 / 10.4) < 1.0e-9);
        std::vector<double> slow(98U, 10.0);
        slow.push_back(40.0);
        slow.push_back(50.0);
        assert(summarize_frame_times(slow).low1_ms == 40.0);
        assert(summarize_frame_times({}).frames == 0);
        assert(summarize_frame_times({}).average_fps() == 0.0);
    }

    // Warm-up frames run first and are not timed; the frame limit holds.
    {
        std::vector<int> seen;
        ViewBenchmarkOptions options;
        options.seconds = 60.0;
        options.max_frames = 20;
        options.warmup_frames = 3;
        const auto r = run_view_benchmark(options, [&](int index) {
            seen.push_back(index);
            return true;
        });
        assert(r.frames == 20 && r.warmup_frames == 3 && !r.stopped);
        assert(seen.size() == 23U);
        for (std::size_t i = 0U; i < seen.size(); ++i) assert(seen[i] == static_cast<int>(i));
        assert(r.gpu_frames == 0U && r.cpu_frames == 0U);  // the callback rendered nothing
    }

    // A failing frame stops the run; earlier frames are reported.
    {
        ViewBenchmarkOptions options;
        options.max_frames = 1000;
        options.warmup_frames = 0;
        const auto r = run_view_benchmark(options, [](int index) { return index < 7; });
        assert(r.stopped && r.frames == 7);
    }

    // The time limit ends an endless run.
    {
        ViewBenchmarkOptions options;
        options.seconds = 0.2;
        options.warmup_frames = 0;
        const auto r = run_view_benchmark(options, [](int) { return true; });
        assert(r.frames > 0 && !r.stopped && r.seconds < 0.5);
    }

    // One full turn per 240 frames.
    assert(benchmark_yaw(0.5F, 0) == 0.5F);
    assert(std::fabs(benchmark_yaw(0.0F, 120) - std::numbers::pi_v<float>) < 1.0e-5F);
    assert(benchmark_yaw(0.25F, 240) == 0.25F);

    // Report.
    {
        std::vector<double> ms(50U, 4.0);
        auto r = summarize_frame_times(ms);
        r.gpu_frames = 55U;
        const auto text = format_view_benchmark(r, "GPU: test chip", 1440, 3120, "MSAA 4x");
        assert(text.find("GPU: test chip") == 0U);
        assert(text.find("1440 x 3120 px, MSAA 4x") != std::string::npos);
        assert(text.find("Average: 250.0 fps (4.00 ms)") != std::string::npos);
        assert(text.find("50 frames") != std::string::npos);
    }
    return 0;
}
