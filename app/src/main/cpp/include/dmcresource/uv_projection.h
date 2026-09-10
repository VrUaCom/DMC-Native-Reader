#pragma once

#include <cstddef>
#include <vector>

#include "dmc_rengine/formats/model_mesh_core.hpp"
#include "dmcresource/mesh.h"

namespace dmcresource::uv_projection {

// Frontend-neutral UV projection. Canonical MOD/SCM parsers own binary reading
// and expose serialized int16x2 UV values; this module only converts that typed
// data into the shared Native Reader render/view contract.
//
// Keep the fixed-point scale owned by the canonical common mesh ABI so Native
// Reader never grows a second format rule for MOD/SCM UV decoding.
template <typename SerializedUvRange>
[[nodiscard]] bool append_uv0(const SerializedUvRange& source,
                              std::vector<Vec2>* output) noexcept {
    if (output == nullptr) return false;

    try {
        if (source.size() > output->max_size() - output->size()) return false;
        output->reserve(output->size() + source.size());

        constexpr float scale =
            dmc::rengine::formats::model_family::MeshCoreAbi::uv_fixed_scale;
        static_assert(scale > 0.0F);

        for (const auto& uv : source) {
            output->push_back({
                static_cast<float>(uv.u) / scale,
                static_cast<float>(uv.v) / scale,
            });
        }
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace dmcresource::uv_projection
