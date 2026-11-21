#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "dmcresource/mesh.h"
#include "dmcresource/motion/cloth_chain.h"
#include "dmcresource/render_scene.h"

namespace dmcresource {
struct Session;
}

// Models that stand on their own (cutscene tracks) posed beyond their own
// motion: cloth chains (a .clt), node worlds borrowed from another model
// (RefMotion), and ropes hung between two joints (ClothSim1D).
namespace dmcresource::motion {

// What skinning a rigged model needs: its rest vertices (materialized scene
// order), the inverse rest world of every node (the palette's left side), its
// rest bone lengths, and its nodes in parent-first order with their parents.
struct ModelSkin final {
    Mesh rest;
    std::vector<Matrix4> inverse_rest;
    std::vector<float> rest_length;
    std::vector<std::uint32_t> order;
    std::vector<std::int32_t> parent;
};

// nullptr when the model has no spatial rig.
[[nodiscard]] std::shared_ptr<ModelSkin> make_model_skin(const Session& model);

// The model's vertices with its nodes at `worlds` (one per node, in the space
// the vertices are wanted in).
[[nodiscard]] bool skin_model(const Session& model, const ModelSkin& skin, std::span<const Matrix4> worlds,
                              std::vector<Vec3>* vertices);

// ---- cloth -------------------------------------------------------------------
//
// A cutscene track's hair, coat or amulet chain whose .clt a Motion clip names
// ("Cloth pl100_01.clt"). The model's motion poses it in its own space, a
// placement puts it in the world (a CnsPos track rides its host joint), and
// the chain's nodes follow the solver of cloth_chain.h there (gravity is world
// down): every other node takes its animated local under its parent's current
// world, as 0x14030E680 composes a chained model.
struct ModelCloth final {
    ClothState state;
    ModelSkin skin;
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

// ---- ropes (ClothSim1D) --------------------------------------------------------
//
// A chain model hung between two joints (m03_b00: Gigapede's chains, em125_05,
// from its body joint to an anchor of the stage; m03_b01: the chain of
// wp115_01). Its .clt is its own dialect:
//   ClothSim1D <segments> <segment length>
//   Gravity x y z / SpringForce k / Damping d / MaxSpeed v / FloorLevel y
//   Cut a b   (the chain broken between points a and b)
// The chain's points are the model's nodes from node 1 on (each the child of
// the one before, rest steps along x: em125_05 150 apart, wp115_01 -13.07);
// the first rides the first joint, the last the second, the others fall
// (Verlet: velocity times Damping, at most MaxSpeed, plus Gravity a frame)
// and keep their rest distances (SpringForce of the stretch back a pass).
struct RopeParams final {
    std::array<float, 3> gravity{0.0F, -1.0F, 0.0F};
    float spring{0.3F};
    float damping{0.97F};
    float max_speed{50.0F};
    bool has_floor{};
    float floor{};
    int cut{-1};  // the segment from point `cut` to the next is broken
};

[[nodiscard]] bool looks_like_rope(std::string_view text) noexcept;
[[nodiscard]] RopeParams parse_rope(std::string_view text);

struct Rope final {
    RopeParams params;
    ModelSkin skin;
    std::vector<std::uint32_t> chain;  // the chain's nodes, point order
    std::vector<float> rest;           // rest distance from each point to the next
    float axis_sign{1.0F};             // the chain runs along +x (1) or -x (-1)
    std::vector<std::array<float, 3>> point;
    std::vector<std::array<float, 3>> previous;
    bool initialized{};
    // The far end's anchor is not in the scene (m03_b00 hangs Gigapede's
    // chains on st006.scm, which the archive comments out: the game uses
    // the stage it is in): the last point holds where the rest pose put it.
    bool hold_last{};
    // That anchor, kept from the first start on (a restart hangs the rope
    // between the first joint and it).
    bool has_anchor{};
    std::array<float, 3> anchor{};
};

// nullptr when the model is not such a chain.
[[nodiscard]] std::shared_ptr<Rope> make_rope(const Session& model, std::string_view clt_text);

// World vertices of the rope hung from `first` and `last` (world; null: that
// end hangs free), stepped `steps` frames (a rope that is not initialized
// starts straight between its ends, or at the rest pose from `first`, and
// settles `steps`).
[[nodiscard]] bool pose_rope(const Session& model, Rope& rope, const Vec3* first, const Vec3* last,
                             std::uint32_t steps, std::vector<Vec3>* world_vertices);

}  // namespace dmcresource::motion
