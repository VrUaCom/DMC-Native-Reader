#include "dmcresource/view_benchmark.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <numeric>

#include "dmcresource/view_gpu.h"

namespace dmcresource {

float benchmark_yaw(float start, int index) noexcept {
    const float turn = 2.0F * std::numbers::pi_v<float> * static_cast<float>(index % 240) / 240.0F;
    return start + turn;
}

ViewBenchmarkResult summarize_frame_times(std::vector<double> frame_ms) {
    ViewBenchmarkResult out;
    out.frames = static_cast<int>(frame_ms.size());
    if (frame_ms.empty()) return out;
    out.seconds = std::accumulate(frame_ms.begin(), frame_ms.end(), 0.0) / 1000.0;
    out.average_ms = out.seconds * 1000.0 / static_cast<double>(frame_ms.size());
    std::sort(frame_ms.begin(), frame_ms.end());
    out.best_ms = frame_ms.front();
    out.worst_ms = frame_ms.back();
    const auto at = [&frame_ms](double q) {
        const auto i = static_cast<std::size_t>(std::ceil(q * static_cast<double>(frame_ms.size()))) ;
        return frame_ms[std::min(frame_ms.size() - 1U, i == 0U ? 0U : i - 1U)];
    };
    out.median_ms = at(0.5);
    out.low1_ms = at(0.99);
    return out;
}

ViewBenchmarkResult run_view_benchmark(const ViewBenchmarkOptions& options, const ViewBenchmarkFrame& frame) {
    using clock = std::chrono::steady_clock;
    bool stopped = false;
    int index = 0;
    const int warmup = std::max(0, options.warmup_frames);
    for (; index < warmup; ++index) {
        if (!frame(index)) {
            stopped = true;
            break;
        }
    }
    const auto before = gpu_view_stats();
    std::vector<double> times;
    times.reserve(1024U);
    const auto begin = clock::now();
    const auto limit = std::chrono::duration<double>(std::max(0.1, options.seconds));
    while (!stopped && static_cast<int>(times.size()) < std::max(1, options.max_frames) &&
           clock::now() - begin < limit) {
        const auto t0 = clock::now();
        if (!frame(index++)) {
            stopped = true;
            break;
        }
        times.push_back(std::chrono::duration<double, std::milli>(clock::now() - t0).count());
    }
    auto out = summarize_frame_times(std::move(times));
    const auto after = gpu_view_stats();
    out.warmup_frames = warmup;
    out.gpu_frames = after.gpu_frames - before.gpu_frames;
    out.cpu_frames = after.cpu_frames - before.cpu_frames;
    out.gpu_failures = after.gpu_failures - before.gpu_failures;
    out.stopped = stopped;
    return out;
}

std::string format_view_benchmark(const ViewBenchmarkResult& r, const std::string& renderer, int width,
                                  int height, const std::string& settings) {
    char text[1024];
    std::snprintf(text, sizeof text,
                  "%s\n%d x %d px%s%s\n"
                  "Average: %.1f fps (%.2f ms)\n"
                  "1%% low: %.1f fps (%.2f ms)\n"
                  "Median: %.2f ms, best %.1f fps, worst %.1f fps\n"
                  "%d frames in %.1f s (GPU %llu, CPU %llu%s)%s",
                  renderer.c_str(), width, height, settings.empty() ? "" : ", ", settings.c_str(),
                  r.average_fps(), r.average_ms, r.low1_fps(), r.low1_ms, r.median_ms, r.best_fps(),
                  r.worst_fps(), r.frames, r.seconds, static_cast<unsigned long long>(r.gpu_frames),
                  static_cast<unsigned long long>(r.cpu_frames),
                  r.gpu_failures != 0U ? (", " + std::to_string(r.gpu_failures) + " GPU fallbacks").c_str() : "",
                  r.stopped ? "\nStopped early: a frame failed." : "");
    return text;
}

}  // namespace dmcresource
