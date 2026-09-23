#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "dmc_rengine/formats/mot/ir.hpp"
#include "dmcresource/motion/animated_local.h"
#include "dmcresource/motion/skeleton_rig.h"

namespace dmcresource::motion {

enum class ClipError : std::uint8_t {
    ParseRejected,
    NoSkeleton,
    NodeCountMismatch,
    BindingRejected,
    AllocationFailed,
};

[[nodiscard]] constexpr const char* to_string(ClipError error) noexcept {
    switch (error) {
        case ClipError::ParseRejected: return "mot-parse-rejected";
        case ClipError::NoSkeleton: return "model-has-no-spatial-skeleton";
        case ClipError::NodeCountMismatch: return "mot-channel-domain-differs-from-model-nodes";
        case ClipError::BindingRejected: return "mot-channel-binding-rejected";
        case ClipError::AllocationFailed: return "allocation-failed";
    }
    return "unknown";
}

struct ClipStats final {
    std::size_t tracks{};
    std::size_t compression2_tracks{};
    std::size_t compression3_tracks{};
    std::size_t unsupported_tracks{};
};

// One MOT bound to one MOD skeleton through the EXE-confirmed normal channel
// binding (0x140310A61). Read-only: the MOT bytes and the MOD rest records are
// never modified.
class MotionClip final {
public:
    [[nodiscard]] static std::expected<MotionClip, ClipError> bind(
        std::span<const std::byte> mot_bytes,
        const SkeletonRig& rig,
        std::string* parse_message = nullptr);

    // Header +0x0C: end frame of the timeline (corpus-confirmed mirror of +0x14).
    [[nodiscard]] float end_frame() const noexcept { return end_frame_; }
    // Header +0x10: bounded secondary scalar, read as loop-start frame when
    // non-zero and smaller than the end frame (community reading; not promoted).
    [[nodiscard]] float loop_start_frame() const noexcept { return loop_start_frame_; }
    [[nodiscard]] std::size_t node_count() const noexcept { return defaults_.size(); }
    [[nodiscard]] const ClipStats& stats() const noexcept { return stats_; }

    // Evaluate every node's nine channels at `frame` and build the animated
    // local matrices (node-indexed). Track caches are updated in place so
    // sequential playback stays O(1) per track, like the runtime cache.
    [[nodiscard]] bool evaluate_locals(float frame, std::span<Matrix4f> out_locals) noexcept;

    [[nodiscard]] bool any_non_unit_scale() const noexcept { return saw_non_unit_scale_; }

private:
    struct ChannelTrack final {
        std::size_t node{};
        std::uint8_t channel{};   // 0..8, normal channel order
        std::size_t track{};
    };

    std::shared_ptr<const dmc::rengine::formats::mot::Document> document_;
    std::vector<JointChannels> defaults_;
    std::vector<ChannelTrack> bound_;
    std::vector<std::int32_t> caches_;
    std::vector<JointChannels> scratch_;
    float end_frame_{};
    float loop_start_frame_{};
    ClipStats stats_{};
    bool saw_non_unit_scale_{false};
};

// Compression-2 evaluator (0x1402E9170 case 2): same cached key search as
// compression 3 (0x1402E8FB0 differs from 0x1402E8C80 only in key stride),
// value = u16 * q1 / 65535 + q0, strictly linear between keys.
[[nodiscard]] bool evaluate_compression2_track(
    const dmc::rengine::formats::mot::TrackRecord& track,
    float frame,
    std::int32_t* cached_index,
    float* out_value) noexcept;

}  // namespace dmcresource::motion
