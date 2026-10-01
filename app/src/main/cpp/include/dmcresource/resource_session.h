#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dmcresource/composite_model.h"
#include "dmcresource/effect_bank.h"
#include "dmcresource/uv_gallery.h"
#include "dmcresource/decode_pipeline.h"
#include "dmcresource/motion/effect_runtime.h"
#include "dmcresource/motion/motion_script.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/motion/uv_scroll.h"
#include "dmcresource/shadow_hull.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/view_renderer.h"
#include "dmcresource/workspace_graph.h"

namespace dmcresource {

namespace motion {
struct MotionState;
}
namespace collision {
struct CollisionBinding;
}
namespace stage_room {
struct Room;
}
namespace environment_collision {
struct Source;
}

// Portable product session; platform shells own only handles and byte transport.
struct Session {
    dmcresource::ProbeResult probe;
    dmcresource::ResourceCapabilities capabilities{};
    dmcresource::InspectionDocument inspection;
    dmcresource::RenderScene scene;
    // Primary visual/playback-facing preview.
    dmcresource::ImagePreview image_preview;
    // Optional evidence/diagnostic view of the same logical effect/resource.
    dmcresource::ImagePreview info_preview;
    bool info_preview_active{};
    std::vector<dmcresource::ChildResource> children;

    dmcresource::Mesh render_mesh;
    dmcresource::HierarchyOverlay hierarchy_overlay;
    std::vector<std::uint32_t> render_triangle_texture_slots;

    // Render-bank indexed decoded textures. For a single model this normally
    // matches canonical source slots. Composite Spider actions may remap triangle
    // slots onto one shared or mixed bank while each CompositePart retains the
    // authoritative source-local slot identity separately.
    std::vector<dmcresource::ImagePreview> attached_textures;
    std::string texture_attachment_detail;
    bool texture_companion_attached{};

    // Native product/workspace authority for stable resource/instance/binding
    // identity. Java URI arrays and flattened render tables are lifecycle or
    // derived presentation state and must not replace this graph.
    WorkspaceGraph workspace_graph;

    // Non-empty only for an explicitly composed multi-MOD scene. Parts retain
    // source-local scene/node/texture namespaces, stable workspace identity and
    // explicit placement state. The top-level Session owns derived render/
    // hierarchy caches only; it is not a second format authority.
    std::vector<CompositePart> composite_parts;

    std::string detail;
    std::string trace;
    // Canonical source archive name for assembled PAC sessions. Runtime
    // MotionScript-to-MOT routing uses it to apply EXE-confirmed group maps
    // where available and deterministic corpus binding otherwise.
    std::string archive_name;
    bool renderable{};
    // A stage archive opened as its assembled scene (stage_room::open_stage):
    // the merged room it was built from, with its HITS collision.
    std::shared_ptr<const stage_room::Room> stage;
    // A HITS file opened on its own: the parsed records, for the kind colours.
    std::shared_ptr<const environment_collision::Source> hits;

    // Motions discovered while assembling a PAC (read-only copies of the
    // retained payloads). Played through motion::load_motion.
    struct MotionPayload final {
        struct ScriptLink final {
            std::size_t script_index{};
            std::size_t bank{};
            std::size_t action{};
            // CEm034 state that starts this action, when recovered. -1 means
            // the action can still be executed by its script controller but
            // no actor-state entry is implied by current evidence.
            int lady_state{-1};
            // Body lane mask: bit0=lane0, bit1=lane1. 0 for non-body scripts.
            std::uint8_t lady_lane_mask{};
        };

        std::string name;
        std::vector<std::uint8_t> bytes;
        // Motion script address (pl000_00_<bank>.pac, MOT index); -1 unknown.
        int bank{-1};
        int index{-1};
        // Motion PAC this MOT sits in (archive slot) and its slot there;
        // -1 when the MOT is not inside a nested motion PAC.
        int pack_slot{-1};
        int mot_slot{-1};
        // Script actions that play it ("act 3,7 loop"), empty when unknown.
        std::string actions;
        // Every script controller in this PAC that references this MOT.
        std::vector<ScriptLink> script_links;
    };
    std::vector<MotionPayload> motion_library;

    // Non-canonical reads the viewer still shows (orange warning in the UI).
    std::vector<std::string> non_canonical_notes;

    // SHW shadow hulls placed on this session's models (PAC assembly).
    std::vector<shadow::ShadowBinding> shadow_bindings;

    enum class MotionScriptRole : std::uint8_t {
        Primary,
        LadyBody,
        LadyComponent0,
    };
    struct MotionScriptBinding final {
        std::uint32_t archive_slot{};
        MotionScriptRole role{MotionScriptRole::Primary};
        std::shared_ptr<const motion::MotionScriptFile> script;
    };

    // Backward-compatible primary script used by player weapon-state playback.
    // Enemy/boss archives may retain multiple independent controllers below.
    std::shared_ptr<const motion::MotionScriptFile> motion_script;
    std::vector<MotionScriptBinding> motion_scripts;
    std::vector<motion::WeaponBinding> weapon_bindings;
    // Enemy class of an em000-style archive position (EnemyVariant) and the
    // composite part its weapon model was attached as (script object 1).
    std::string enemy_class;
    std::optional<std::size_t> enemy_weapon_part;

    // Boss-Lady CEm034 uses a different runtime: five persistent component
    // managers with two placement presets, plus separate dynamic CShell actors.
    // Keep this separate from player WeaponBinding so a single scalar weapon
    // state cannot silently collapse the recovered multi-channel contract.
    std::vector<motion::LadyComponentBinding> lady_component_bindings;

    // Script-owned effect state. Raw MOT playback never feeds this runtime;
    // profile bridges provide evidence-backed bindings and Script Play is the
    // only producer. Visibility is presentation-only and must not reset state.
    std::shared_ptr<motion::EffectRuntime> effect_runtime;
    bool effects_visible{true};
    // Owned profile data used by the generic installer. Each profile may
    // replace this table when its Script controller is selected; MotionPlayer
    // does not inspect character names or effect semantics.
    std::vector<motion::EffectBinding> script_effect_bindings;
    // Session-owned storage for nested binding spans. Profile providers may
    // supply temporary vectors; the session must retain the full graph before
    // Script Play or runtime construction can outlive the provider call.
    std::vector<std::shared_ptr<std::vector<motion::EffectChildRef>>>
        script_effect_child_groups;
    // Optional profile-owned producer for any Script Play profile. Its
    // prepare/reset/step hooks are called only by Script Play; raw MOT never
    // enters this path. The producer feeds the one shared EffectRuntime.
    motion::ScriptEffectBridge script_effect_bridge{};
    // Direct PAC slots whose payload was identified as an EXE FXBANK. Runtime
    // bindings may only resolve against these retained resource identities;
    // an absent slot is never substituted with another bank or child record.
    std::vector<std::uint32_t> effect_bank_slots;
    // Exact `(kind,u16 id)` keys parsed from those banks. This is a resource
    // availability gate, not a semantic effect-name table.
    std::vector<motion::EffectResourceRef> effect_resources;

    // Retained resource-backed FXBANK data. The parsed record spans point into
    // `source`, so a runtime presentation lookup never falls back to a
    // different bank or to a synthetic effect resource.
    struct EffectTexture final {
        std::uint16_t id{};
        ImagePreview image;
    };
    struct EffectBank final {
        std::uint32_t resource_slot{};
        std::shared_ptr<const std::vector<std::uint8_t>> source;
        effect_bank::Bank bank;
        std::vector<EffectTexture> textures;
    };
    std::vector<EffectBank> effect_banks;

    // Dynamic CEm034 CShell visuals stay outside the persistent composite.
    // Source geometry/textures are retained once; active/world are presentation
    // state driven only by the recovered Shl actor lifecycle.
    struct LadyDynamicVisual final {
        std::uint8_t actor{};   // CEm034Shl00..05 index
        std::uint32_t model_slot{};
        // Preserve the canonical MOD scene for dynamic skeletal deformation
        // (slot30 uses its five-node skin domain). source_mesh is the retained
        // flattened rest projection consumed by the presentation layer.
        RenderScene source_scene;
        Mesh source_mesh;
        std::vector<std::uint32_t> texture_slots;
        std::vector<ImagePreview> textures;
        Matrix4 world{};
        // Retail CEm034Shl02 passes a separately normalized copy of the
        // selected slot20 matrix to V423. It is not the actor render basis.
        Matrix4 effect_parent_world{};
        // Shl03: velocity per tick. Shl02: unit flight direction (shell+0x140
        // before the speed factor) and the init position (shell+0x80).
        Vec3 velocity{};
        Vec3 origin{};
        // CShell state byte (+0x08): 1 flight, 2 explode.
        std::uint8_t shell_state{};
        bool explode_emitted{};
        // Shl02: flight age at which the shell met the stage HITS (<0 none).
        float hit_age{-1.0F};
        // lane/channel/value of the MotionScript signal that spawned it.
        std::array<std::uint8_t, 3> spawn_signal{0xFFU, 0xFFU, 0xFFU};
        float spawn_frame{-1.0F};
        float last_update_frame{-1.0F};
        float retire_frame{-1.0F};  // <0 = owner/state controlled
        bool active{};
        // slot30 shares Shl03 actor transform but has an additional internal
        // tether deformation domain that is not yet claimed pixel-exact.
        bool exact_deformation{true};
    };
    std::vector<LadyDynamicVisual> lady_dynamic_visuals;

    // Attack collision handle (index + shapes) on the body bones; drawn with
    // RenderFlag::Collision (collision_debug.h).
    std::shared_ptr<collision::CollisionBinding> collision;

    // TSC texture scroll ranges (CDrawUV), advanced with motion playback.
    std::vector<motion::UvScrollBinding> uv_scrolls;

    // Bound MOT playback state (read-only preview; see motion/motion_player.h).
    std::shared_ptr<motion::MotionState> motion;

    std::shared_ptr<const UvGallery> uv_gallery;
    std::optional<std::size_t> uv_map_index;
};

[[nodiscard]] std::unique_ptr<Session> open_session(std::string_view name,
    const std::uint8_t* bytes, std::size_t size);
[[nodiscard]] std::unique_ptr<Session> session_from_child(const ChildResource& child);
[[nodiscard]] std::unique_ptr<Session> open_uv_gallery(const Session* model);

// Low-level composition primitive used by the Spider session-action layer.
// Platform/JNI callers must use spider::actions::compose_mod_sessions instead.
[[nodiscard]] std::unique_ptr<Session> compose_mod_sessions(
    const std::vector<const Session*>& parts,
    const std::vector<std::string>& names);

[[nodiscard]] std::size_t session_composite_part_count(const Session* session) noexcept;
[[nodiscard]] std::string session_composite_part_name(const Session* session, int index);
[[nodiscard]] std::size_t session_child_count(const Session* session) noexcept;
[[nodiscard]] std::string session_child_title(const Session* session, int index);
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> session_child_preview_size(
    const Session* session, int index) noexcept;
// PTX previews are borrowed. Generated previews use caller-owned scratch only.
[[nodiscard]] const ImagePreview* session_child_preview(
    const Session* session, int index, ImagePreview* scratch);
[[nodiscard]] std::unique_ptr<Session> open_session_child(const Session* session, int index);

// Dual Visual / Info surface. The active preview is the image exposed to
// platform shells; sessions without info_preview behave exactly as before.
[[nodiscard]] bool session_has_dual_preview(const Session* session) noexcept;
[[nodiscard]] bool session_info_preview_active(const Session* session) noexcept;
[[nodiscard]] bool session_set_info_preview(Session* session, bool info) noexcept;
[[nodiscard]] const ImagePreview* session_active_preview(const Session* session) noexcept;
[[nodiscard]] std::string describe_session(const Session* session);
[[nodiscard]] spider::black_widow::StateBits black_widow_state(const Session* session) noexcept;

// Texture attachment is intentionally absent from this generic session API.
// Product attachment actions are owned by dmcresource::spider::actions so JNI,
// desktop shells and future platforms cannot bypass the Spider execution layer.
[[nodiscard]] RgbaImage render_session(const Session* session, int requested_width,
    int requested_height, float yaw, float pitch, float zoom, std::uint32_t render_flags);

// Camera and room controls from the viewer's gestures.
struct ViewControls final {
    float pan_x{};     // camera-plane shift, in framing radii
    float pan_y{};
    float room_yaw{};  // room turned about the model's spot (radians)
    bool follow{};     // camera follows the model as its motion moves it
    // Camera dolly as a fraction of the framing distance (session_camera_distance):
    // >0 moves the camera toward the model, <0 away.
    float dolly{};
};

// Model-unit distance of the camera from the framed model at dolly 0 (0 when
// the session has nothing to frame).
[[nodiscard]] float session_camera_distance(const Session* session) noexcept;
// Dolly range of a session: a stage scene can be entered, a model cannot be
// passed through.
[[nodiscard]] float session_dolly_limit(const Session* session) noexcept;

[[nodiscard]] RgbaImage render_session(const Session* session, int requested_width,
    int requested_height, float yaw, float pitch, float zoom, std::uint32_t render_flags,
    const ViewControls& controls);

// What lies under image pixel (x, y) of that same view: the model, the room
// (with the room point) and the nearest drawn joint ("joint 9 · name (part)").
struct SessionPick final {
    bool model{};
    bool room{};
    bool room_floor{};
    Vec3 room_point{};
    int joint{-1};
    std::string joint_name;
};

[[nodiscard]] SessionPick pick_session(const Session* session, int requested_width,
    int requested_height, float yaw, float pitch, float zoom, std::uint32_t render_flags,
    const ViewControls& controls, float x, float y);

}  // namespace dmcresource
