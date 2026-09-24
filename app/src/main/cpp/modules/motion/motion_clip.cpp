#include "dmcresource/motion/motion_clip.h"

#include <cmath>
#include <limits>
#include <new>
#include <utility>

#include "dmc_rengine/analysis/mot/channel_binding.hpp"
#include "dmc_rengine/analysis/mot/key_decode.hpp"
#include "dmc_rengine/analysis/mot/track_evaluation.hpp"
#include "dmc_rengine/formats/mot/parser.hpp"

namespace dmcresource::motion {
namespace {

namespace mot = dmc::rengine::formats::mot;
namespace mot_analysis = dmc::rengine::analysis::mot;

struct Selection final {
    std::size_t left{};
    std::size_t right{};
    bool segment{false};
    std::int32_t cache{};
};

// 0x1402E8FB0: identical control flow to the compression-3 search 0x1402E8C80
// with a 4-byte key stride. Times are the low 15 bits of the control word.
[[nodiscard]] bool select_cached_segment2(const mot::TrackRecord& track,
                                          float local_time,
                                          std::int32_t cached_index,
                                          Selection* out) noexcept {
    if (out == nullptr || track.key_count == 0U ||
        track.keys2.size() < static_cast<std::size_t>(track.key_count) ||
        !std::isfinite(local_time) || cached_index < 0 ||
        cached_index >= static_cast<std::int32_t>(track.key_count)) {
        return false;
    }
    const auto count = static_cast<std::size_t>(track.key_count);
    const auto key_time = [&track](std::size_t index) noexcept {
        return static_cast<float>(track.keys2[index].time_control & 0x7FFFU);
    };

    std::size_t index = static_cast<std::size_t>(cached_index);
    if (local_time >= key_time(index)) {
        while (index < count - 1U) {
            if (key_time(index + 1U) > local_time) {
                *out = {index, index + 1U, true, static_cast<std::int32_t>(index)};
                return true;
            }
            if (key_time(index) == local_time) {
                *out = {index, index, false, static_cast<std::int32_t>(index)};
                return true;
            }
            ++index;
        }
        *out = {index, index, false, static_cast<std::int32_t>(count - 1U)};
        return true;
    }
    while (index >= 1U) {
        const auto previous = index - 1U;
        if (local_time > key_time(previous)) {
            *out = {previous, index, true, static_cast<std::int32_t>(previous)};
            return true;
        }
        if (local_time == key_time(previous)) {
            *out = {previous, previous, false, static_cast<std::int32_t>(previous)};
            return true;
        }
        index = previous;
    }
    *out = {0U, 0U, false, 0};
    return true;
}

[[nodiscard]] float default_channel(const JointChannels& channels, std::uint8_t channel) noexcept {
    if (channel < 3U) return channels.translation[channel];
    if (channel < 6U) return channels.rotation[channel - 3U];
    return channels.scale[channel - 6U];
}

void set_channel(JointChannels* channels, std::uint8_t channel, float value) noexcept {
    if (channel < 3U) channels->translation[channel] = value;
    else if (channel < 6U) channels->rotation[channel - 3U] = value;
    else channels->scale[channel - 6U] = value;
}

}  // namespace

bool evaluate_compression2_track(const mot::TrackRecord& track,
                                 float frame,
                                 std::int32_t* cached_index,
                                 float* out_value) noexcept {
    if (cached_index == nullptr || out_value == nullptr || track.compression != 2U ||
        track.quantization_float_count != 2U || !std::isfinite(frame)) {
        return false;
    }
    const float local_time =
        frame - static_cast<float>(mot_analysis::signed_track_time_offset(track.start_time_raw));
    Selection selection;
    if (!select_cached_segment2(track, local_time, *cached_index, &selection)) return false;

    const auto left = mot_analysis::decode_key(track, selection.left);
    if (!left.has_value()) return false;
    float value = left->value;
    if (selection.segment) {
        const auto right = mot_analysis::decode_key(track, selection.right);
        if (!right.has_value()) return false;
        const float t0 = static_cast<float>(track.keys2[selection.left].time_control & 0x7FFFU);
        const float t1 = static_cast<float>(track.keys2[selection.right].time_control & 0x7FFFU);
        if (!(t1 > t0)) return false;
        const float u = (local_time - t0) / (t1 - t0);
        value = (1.0F - u) * left->value + u * right->value;
    }
    if (!std::isfinite(value)) return false;
    *cached_index = selection.cache;
    *out_value = value;
    return true;
}

std::expected<MotionClip, ClipError> MotionClip::bind(
    std::span<const std::byte> mot_bytes,
    const SkeletonRig& rig,
    std::string* parse_message) {
    try {
        auto parsed = mot::Parser::parse(mot_bytes);
        if (parse_message != nullptr) *parse_message = parsed.message;
        if (!parsed.ok()) return std::unexpected(ClipError::ParseRejected);

        const auto node_count = rig.node_count();
        if (node_count == 0U) return std::unexpected(ClipError::NoSkeleton);
        // 0x140310A61 walks every CMotionJoint, consumes one channel mask per
        // joint and binds only joints whose motion group (+0xF8) is the one
        // being evaluated; joints of other groups skip their tracks. A MOT
        // whose domain covers the leading joints therefore drives a model
        // with extra trailing joints when those joints belong to motion groups
        // none of the covered joints uses (em000 bodies: 22-node MOTs, node 22
        // in group 2). The extra joints keep their rest locals.
        const auto domain = static_cast<std::size_t>(parsed.document->channel_domain_count);
        if (domain != node_count) {
            if (domain == 0U || domain > node_count || rig.motion_group_by_node.size() != node_count) {
                return std::unexpected(ClipError::NodeCountMismatch);
            }
            for (std::size_t extra = domain; extra < node_count; ++extra) {
                for (std::size_t covered = 0U; covered < domain; ++covered) {
                    if (rig.motion_group_by_node[covered] == rig.motion_group_by_node[extra]) {
                        return std::unexpected(ClipError::NodeCountMismatch);
                    }
                }
            }
        }
        const auto binding = mot_analysis::project_normal_binding(*parsed.document, domain);
        if (!binding.has_value()) return std::unexpected(ClipError::BindingRejected);

        MotionClip clip;
        clip.document_ = std::make_shared<const mot::Document>(std::move(*parsed.document));
        clip.defaults_.resize(node_count);
        for (std::size_t node = 0U; node < node_count; ++node) {
            // 0x14030F800: defaults are the MOD rest T/R records, scale 1.0.
            const auto& record = rig.domain.local_transform_records_by_node_index[node];
            auto& defaults = clip.defaults_[node];
            defaults.translation = {record.translation.x, record.translation.y,
                                    record.translation.z};
            defaults.rotation = {record.rotation_xyz_radians.x,
                                 record.rotation_xyz_radians.y,
                                 record.rotation_xyz_radians.z};
            defaults.scale = {1.0F, 1.0F, 1.0F};
        }

        clip.bound_.reserve(binding->tracks.size());
        for (const auto& bound : binding->tracks) {
            if (bound.node_index >= node_count ||
                bound.track_index >= clip.document_->tracks.size()) {
                return std::unexpected(ClipError::BindingRejected);
            }
            const auto& track = clip.document_->tracks[bound.track_index];
            ++clip.stats_.tracks;
            if (track.compression == 3U) ++clip.stats_.compression3_tracks;
            else if (track.compression == 2U) ++clip.stats_.compression2_tracks;
            else ++clip.stats_.unsupported_tracks;
            clip.bound_.push_back({bound.node_index,
                                   static_cast<std::uint8_t>(std::to_underlying(bound.channel)),
                                   bound.track_index});
        }
        clip.caches_.assign(clip.document_->tracks.size(), 0);
        clip.scratch_ = clip.defaults_;

        const float end = clip.document_->raw_f32_0c;
        clip.end_frame_ = std::isfinite(end) && end > 0.0F ? end : 0.0F;
        const float loop = clip.document_->raw_f32_10;
        clip.loop_start_frame_ =
            std::isfinite(loop) && loop > 0.0F && loop < clip.end_frame_ ? loop : 0.0F;
        return clip;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ClipError::AllocationFailed);
    } catch (...) {
        return std::unexpected(ClipError::ParseRejected);
    }
}

bool MotionClip::evaluate_locals(float frame, std::span<Matrix4f> out_locals) noexcept {
    if (document_ == nullptr || out_locals.size() != defaults_.size() || !std::isfinite(frame)) {
        return false;
    }
    for (std::size_t node = 0U; node < defaults_.size(); ++node) scratch_[node] = defaults_[node];

    for (const auto& bound : bound_) {
        const auto& track = document_->tracks[bound.track];
        auto& cache = caches_[bound.track];
        float value = default_channel(defaults_[bound.node], bound.channel);
        if (track.compression == 3U) {
            const auto evaluated = mot_analysis::evaluate_compression3_track(track, frame, cache);
            if (evaluated.has_value() && std::isfinite(evaluated->value)) {
                value = evaluated->value;
                cache = evaluated->cached_index;
            }
        } else if (track.compression == 2U) {
            float evaluated = 0.0F;
            if (evaluate_compression2_track(track, frame, &cache, &evaluated)) value = evaluated;
        }
        set_channel(&scratch_[bound.node], bound.channel, value);
    }

    for (std::size_t node = 0U; node < defaults_.size(); ++node) {
        if (has_non_unit_scale(scratch_[node])) saw_non_unit_scale_ = true;
        out_locals[node] = build_animated_local_matrix(scratch_[node]);
    }
    return true;
}

}  // namespace dmcresource::motion
