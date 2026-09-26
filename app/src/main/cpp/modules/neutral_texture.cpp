#include "dmcresource/neutral_texture.h"

namespace dmcresource {

const ImagePreview& neutral_texture() {
    static const ImagePreview texture = [] {
        ImagePreview out;
        out.width = kNeutralTextureWidth;
        out.height = kNeutralTextureHeight;
        out.rgba8.resize(static_cast<std::size_t>(out.width) * out.height * 4U);
        for (std::size_t i = 0U; i < out.rgba8.size(); i += 4U) {
            out.rgba8[i + 0U] = kNeutralGrey;
            out.rgba8[i + 1U] = kNeutralGrey;
            out.rgba8[i + 2U] = kNeutralGrey;
            out.rgba8[i + 3U] = 0xFFU;
        }
        return out;
    }();
    return texture;
}

}  // namespace dmcresource
