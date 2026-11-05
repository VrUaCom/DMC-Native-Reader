#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// The DMC3 shaders: our own GLSL ES 3.00 shaders (shaders/dmc3/) that behave
// like the HD port's, written clean-room from behaviour specifications
// (docs/cleanroom/). One entry each: our name, the name of the game's shader
// it stands for, and the source, built into the library from the files.
// docs/SHADERS.md is the map, and says which of them the Reader draws with.
namespace dmcresource::dmc3 {

enum class ShaderStage : std::uint8_t { vertex, fragment };

enum class ShaderFamily : std::uint8_t {
    model,         // skinned characters (MOD)
    effect_model,  // skinned effect models (EFM)
    stage,         // the stage (SCM) and its point lights
    shadow,        // shadow volumes
    particle,      // particles
    flat,          // 2D / flat geometry
    texture,       // the PS2 texture functions (fragment)
    screen,        // full-screen effects
    copy,          // buffer copies
    movie,         // movie frames
};

// Named after our files: model_lit.vert is model_lit_vert.
enum class Shader : std::uint8_t {
    model_lit_vert,
    model_lit_sheen_vert,
    model_screen_uv_vert,
    effect_model_lit_vert,
    effect_model_lit_sheen_vert,
    effect_model_screen_uv_vert,
    effect_model_vertex_alpha_vert,
    effect_model_vertex_alpha_sheen_vert,
    stage_lit_vert,
    stage_lit_fog_vert,
    shadow_volume_vert,
    particle_vert,
    particle_fog_vert,
    flat_object_colour_vert,
    flat_object_colour_fog_vert,
    flat_textured_vert,
    flat_textured_fog_vert,
    flat_textured_object_colour_vert,
    flat_textured_object_colour_fog_vert,
    flat_textured_vertex_colour_vert,
    flat_textured_vertex_colour_fog_vert,
    flat_vertex_colour_vert,
    flat_vertex_colour_fog_vert,
    screen_pass_depth_of_field_vert,
    screen_pass_directional_vert,
    screen_pass_one_param_vert,
    screen_pass_plain_vert,
    screen_pass_two_params_vert,
    movie_frame_vert,
    particle_alpha_boost_frag,
    palette_lookup_frag,
    texture_decal_frag,
    texture_decal_cutout_frag,
    texture_decal_fog_frag,
    texture_decal_fog_cutout_frag,
    texture_decal_opaque_frag,
    texture_highlight_frag,
    texture_highlight2_frag,
    texture_highlight2_fog_frag,
    texture_highlight_fog_frag,
    texture_modulate_frag,
    texture_modulate_cutout_frag,
    texture_modulate_fog_frag,
    texture_modulate_fog_cutout_frag,
    texture_recolour_frag,
    texture_recolour_cutout_frag,
    texture_recolour_fog_frag,
    texture_recolour_fog_cutout_frag,
    vertex_colour_frag,
    colour_grade_blend_frag,
    colour_grade_mask_frag,
    depth_fog_table_frag,
    depth_of_field_frag,
    directional_glow_frag,
    edge_mask_frag,
    edge_smooth_frag,
    glare_four_taps_frag,
    glow_four_taps_frag,
    glow_source_frag,
    object_edge_frag,
    object_edge_directional_frag,
    procedural_fog_frag,
    star_glare_frag,
    copy_colour_frag,
    copy_colour_and_depth_frag,
    copy_depth_frag,
    movie_frame_frag,
    count,
    none = 0xFF,
};

struct ShaderInfo final {
    Shader id{Shader::none};
    const char* name{};       // our file, "model_lit.vert"
    const char* dmc3_name{};  // the HD port's shader it behaves like, "dmc3_mod"
    ShaderStage stage{};
    ShaderFamily family{};
    const char* role{};       // what it draws
};

// Every shader, in the order of the enum.
[[nodiscard]] std::span<const ShaderInfo> shaders() noexcept;
// Null for none.
[[nodiscard]] const ShaderInfo* info(Shader shader) noexcept;
// By our name ("model_lit.vert"; "texture_modulate" when only one stage has
// it) or by the DMC3 name ("dmc3_mod"); none when unknown.
[[nodiscard]] Shader find(std::string_view name) noexcept;
// The GLSL text; empty when the build has no file for it.
[[nodiscard]] std::string_view source(Shader shader) noexcept;
[[nodiscard]] std::string_view family_name(ShaderFamily family) noexcept;

// The alpha test functions of the fragment shaders (u_alpha_func).
enum class AlphaFunc : std::int32_t {
    off = 0,  // u_alpha_test 0
    never = 1,
    less = 2,
    equal = 3,
    less_equal = 4,
    greater = 5,
    not_equal = 6,
    greater_equal = 7,
};

// The sources built into the library (generated from shaders/dmc3/).
namespace detail {
struct EmbeddedSource final {
    const char* file;
    const char* text;
};
[[nodiscard]] std::span<const EmbeddedSource> embedded_sources() noexcept;
}  // namespace detail

}  // namespace dmcresource::dmc3
