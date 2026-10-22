#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "dmcresource/mesh.h"
#include "dmcresource/motion/cloth_chain.h"
#include "dmcresource/render_scene.h"

namespace dmcresource {
struct Session;
}

// Cloth on a model that stands on its own: a cutscene track (hair, coat,
// amulet chain) whose .clt a Motion clip names ("Cloth pl100_01.clt"). The
// model's motion poses it in its own space, a placement puts it in the world
// (a CnsPos track rides its host joint), and the chain's nodes follow the
// solver of cloth_chain.h there (gravity is world down): every other node
// takes its animated local under its parent's current world, as 0x14030E680
// composes a chained model.
namespace dmcresource::motion {

struct ModelCloth final {
    ClothState state;
    // The model's rest vertices (materialized scene order) and the inverse
    // rest world of every node (the skin palette's left side).
    Mesh rest;
    std::vector<Matrix4> inverse_rest;
    // Rest bone lengths (the solver keeps them) and the nodes in parent-first
    // order with their parents.
    std::vector<float> rest_length;
    std::vector<std::uint32_t> order;
    std::vector<std::int32_t> parent;
};

// The cloth of a rigged model: the .clt blocks over its nodes; nullptr when
// the model has no spatial rig or the text lists none of its nodes.
[[nodiscard]] std::shared_ptr<ModelCloth> make_model_cloth(const Session& model, std::string_view clt_text);

// World vertices of `model` (its node worlds as its motion left them, model
// space) placed by `placement` (world = model x placement) with its cloth
// nodes simulated `steps` solver steps (one a game frame). 0 steps keeps the
// last result; a state that is not initialized starts at the targets and
// settles `steps`. False when the model cannot be skinned this way.
[[nodiscard]] bool pose_model_cloth(const Session& model, const Matrix4& placement, ModelCloth& cloth,
                                    std::uint32_t steps, std::vector<Vec3>* world_vertices);

}  // namespace dmcresource::motion
