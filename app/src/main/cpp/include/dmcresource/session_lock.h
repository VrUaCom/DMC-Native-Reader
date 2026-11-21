#pragma once

#include <mutex>

namespace dmcresource {

// The one lock over open sessions, shared by every shell boundary (JNI, the C
// API): a frame is drawn under it, and every call that reads or changes a
// session holds it for the whole use. Recursive, so a call made under the lock
// may call another that takes it. State the core keeps outside sessions (the
// stage room, the camera) has its own locks and needs none of this one.
[[nodiscard]] std::recursive_mutex& session_mutex() noexcept;

using SessionLock = std::lock_guard<std::recursive_mutex>;

}  // namespace dmcresource
