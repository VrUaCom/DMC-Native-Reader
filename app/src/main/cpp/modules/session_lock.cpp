#include "dmcresource/session_lock.h"

namespace dmcresource {

std::recursive_mutex& session_mutex() noexcept {
    static std::recursive_mutex mutex;
    return mutex;
}

}  // namespace dmcresource
