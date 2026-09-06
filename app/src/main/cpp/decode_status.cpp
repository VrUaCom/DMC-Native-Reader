#include "dmcresource/decode.h"

namespace dmcresource {

const char* decode_status_name(DecodeStatus status) noexcept {
    switch (status) {
        case DecodeStatus::Ok: return "OK";
        case DecodeStatus::UnknownFormat: return "UNKNOWN_FORMAT";
        case DecodeStatus::InvalidData: return "INVALID_DATA";
        case DecodeStatus::UnsupportedLayout: return "UNSUPPORTED_LAYOUT";
        case DecodeStatus::TooLarge: return "TOO_LARGE";
        default: return "UNKNOWN";
    }
}

}  // namespace dmcresource
