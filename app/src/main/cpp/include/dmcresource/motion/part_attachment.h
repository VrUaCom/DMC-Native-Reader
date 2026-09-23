#pragma once

#include <cstddef>
#include <cstdint>

namespace dmcresource {
struct Session;
}

namespace dmcresource::motion {

// IPlayer coat reconstruction (reverse authority: dmc-rengine-cpp
// docs/research/dmc3-player-coat-attachment-2026-09-23.md):
//   CPlVergil 0x140225A16 / CPlDante 0x1402120A7 / CPlNewVergil 0x1402204E9
//   call coatModel->vtbl[+0x190](bodyJoint[3]->world) every frame, after the
//   coat root joint's local (+0x108) was set to identity at load
//   (0x140226118..0x140226158). The coat model is PAC slot 12 and uses the
//   body texture of PAC slot 0 (0x1402260A4..0x1402260F7).
inline constexpr std::uint32_t kPlayerCoatHostJoint = 3U;
inline constexpr std::uint32_t kPlayerBodySlot = 1U;
inline constexpr std::uint32_t kPlayerCoatSlot = 12U;
inline constexpr std::uint32_t kPlayerTextureSlot = 0U;

// Hang `child_part`'s skeleton from `host_joint` of `host_part` and pose it
// immediately from the host joint's current world. Read-only: only the
// derived composite projection changes.
[[nodiscard]] bool attach_part_skeleton(Session* session,
                                        std::size_t host_part,
                                        std::size_t child_part,
                                        std::uint32_t host_joint,
                                        bool root_local_identity) noexcept;

// Re-pose every HostJointSkeleton part from its host joint's current world.
// Called after the host moved (MOT frame) and after attachment.
[[nodiscard]] bool apply_part_attachments(Session* session) noexcept;

[[nodiscard]] bool is_attached_part(const Session* session, std::size_t part) noexcept;

}  // namespace dmcresource::motion
