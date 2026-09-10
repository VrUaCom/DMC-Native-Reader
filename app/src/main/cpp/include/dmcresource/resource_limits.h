#pragma once
#include <cstddef>
namespace dmcresource::resource_limits {
inline constexpr std::size_t kMaxResourceBytes = 512U * 1024U * 1024U;
inline constexpr std::size_t kMaxVertices = 2U * 1024U * 1024U;
inline constexpr std::size_t kMaxIndices = 12U * 1024U * 1024U;
}
