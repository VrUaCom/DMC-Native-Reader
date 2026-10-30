// The DMC3 shader library (dmc3_shaders.h).
#include "dmcresource/dmc3_shaders.h"

#include <array>
#include <cstring>

namespace dmcresource::dmc3 {
namespace {

constexpr std::array<ShaderInfo, static_cast<std::size_t>(Shader::count)> kShaders{{
    {Shader::model_lit_vert, "model_lit.vert", "dmc3_mod", ShaderStage::vertex, ShaderFamily::model,
     "Skinned model, lit"},
    {Shader::model_lit_sheen_vert, "model_lit_sheen.vert", "dmc3_mod_sp", ShaderStage::vertex, ShaderFamily::model,
     "Skinned model, lit, with sheen"},
    {Shader::model_screen_uv_vert, "model_screen_uv.vert", "dmc3_mod_stx", ShaderStage::vertex, ShaderFamily::model,
     "Skinned model, screen-space texture (haze / camouflage)"},
    {Shader::effect_model_lit_vert, "effect_model_lit.vert", "dmc3_efm", ShaderStage::vertex, ShaderFamily::effect_model,
     "Skinned effect model, lit, with vertex colour"},
    {Shader::effect_model_lit_sheen_vert, "effect_model_lit_sheen.vert", "dmc3_efm_sp", ShaderStage::vertex, ShaderFamily::effect_model,
     "Skinned effect model, lit, with vertex colour and sheen"},
    {Shader::effect_model_screen_uv_vert, "effect_model_screen_uv.vert", "dmc3_efm_stx", ShaderStage::vertex, ShaderFamily::effect_model,
     "Skinned effect model, screen-space texture (haze / camouflage)"},
    {Shader::effect_model_vertex_alpha_vert, "effect_model_vertex_alpha.vert", "dmc3_efm_va", ShaderStage::vertex, ShaderFamily::effect_model,
     "Skinned effect model, lit, vertex colour and vertex alpha"},
    {Shader::effect_model_vertex_alpha_sheen_vert, "effect_model_vertex_alpha_sheen.vert", "dmc3_efm_va_sp", ShaderStage::vertex, ShaderFamily::effect_model,
     "Skinned effect model, lit, vertex colour and alpha, sheen"},
    {Shader::stage_lit_vert, "stage_lit.vert", "dmc3_stg", ShaderStage::vertex, ShaderFamily::stage,
     "Stage, four point lights, no fog"},
    {Shader::stage_lit_fog_vert, "stage_lit_fog.vert", "dmc3_stg_fog", ShaderStage::vertex, ShaderFamily::stage,
     "Stage, four point lights, distance fog"},
    {Shader::shadow_volume_vert, "shadow_volume.vert", "dmc3_shw", ShaderStage::vertex, ShaderFamily::shadow,
     "Shadow volume"},
    {Shader::particle_vert, "particle.vert", "dmc3_ptcl", ShaderStage::vertex, ShaderFamily::particle,
     "Particle quad"},
    {Shader::particle_fog_vert, "particle_fog.vert", "dmc3_ptcl_fog", ShaderStage::vertex, ShaderFamily::particle,
     "Particle quad with distance fog"},
    {Shader::flat_object_colour_vert, "flat_object_colour.vert", "ocpl", ShaderStage::vertex, ShaderFamily::flat,
     "Flat geometry, one colour"},
    {Shader::flat_object_colour_fog_vert, "flat_object_colour_fog.vert", "ocpl_fog", ShaderStage::vertex, ShaderFamily::flat,
     "Flat geometry, one colour, fog"},
    {Shader::flat_textured_vert, "flat_textured.vert", "vtpl", ShaderStage::vertex, ShaderFamily::flat,
     "Flat geometry, textured"},
    {Shader::flat_textured_fog_vert, "flat_textured_fog.vert", "vtpl_fog", ShaderStage::vertex, ShaderFamily::flat,
     "Flat geometry, textured, fog"},
    {Shader::flat_textured_object_colour_vert, "flat_textured_object_colour.vert", "vtocpl", ShaderStage::vertex, ShaderFamily::flat,
     "Flat geometry, textured, one colour"},
    {Shader::flat_textured_object_colour_fog_vert, "flat_textured_object_colour_fog.vert", "vtocpl_fog", ShaderStage::vertex, ShaderFamily::flat,
     "Flat geometry, textured, one colour, fog"},
    {Shader::flat_textured_vertex_colour_vert, "flat_textured_vertex_colour.vert", "vtvcpl", ShaderStage::vertex, ShaderFamily::flat,
     "Flat geometry, textured, vertex colours"},
    {Shader::flat_textured_vertex_colour_fog_vert, "flat_textured_vertex_colour_fog.vert", "vtvcpl_fog", ShaderStage::vertex, ShaderFamily::flat,
     "Flat geometry, textured, vertex colours, fog"},
    {Shader::flat_vertex_colour_vert, "flat_vertex_colour.vert", "vcpl", ShaderStage::vertex, ShaderFamily::flat,
     "Flat geometry, vertex colours"},
    {Shader::flat_vertex_colour_fog_vert, "flat_vertex_colour_fog.vert", "vcpl_fog", ShaderStage::vertex, ShaderFamily::flat,
     "Flat geometry, vertex colours, fog"},
    {Shader::screen_pass_depth_of_field_vert, "screen_pass_depth_of_field.vert", "fx_dof_v", ShaderStage::vertex, ShaderFamily::screen,
     "Full-screen pass for depth of field"},
    {Shader::screen_pass_directional_vert, "screen_pass_directional.vert", "fx_dir_glow_v", ShaderStage::vertex, ShaderFamily::screen,
     "Full-screen pass, two coordinate sets"},
    {Shader::screen_pass_one_param_vert, "screen_pass_one_param.vert", "fx_1x4f_pass", ShaderStage::vertex, ShaderFamily::screen,
     "Full-screen pass, one parameter"},
    {Shader::screen_pass_plain_vert, "screen_pass_plain.vert", "fx_col_corr_v", ShaderStage::vertex, ShaderFamily::screen,
     "Full-screen pass, coordinates only"},
    {Shader::screen_pass_two_params_vert, "screen_pass_two_params.vert", "fx_2x4f_pass", ShaderStage::vertex, ShaderFamily::screen,
     "Full-screen pass, two parameters"},
    {Shader::movie_frame_vert, "movie_frame.vert", "video_frame_v", ShaderStage::vertex, ShaderFamily::movie,
     "Movie frame"},
    {Shader::particle_alpha_boost_frag, "particle_alpha_boost.frag", "tex_ptcl_end", ShaderStage::fragment, ShaderFamily::particle,
     "Particle texture, alpha raised"},
    {Shader::palette_lookup_frag, "palette_lookup.frag", "fx_decal_clut", ShaderStage::fragment, ShaderFamily::texture,
     "8-bit picture through a palette"},
    {Shader::texture_decal_frag, "texture_decal.frag", "tex_decal", ShaderStage::fragment, ShaderFamily::texture,
     "Texture as it is"},
    {Shader::texture_decal_cutout_frag, "texture_decal_cutout.frag", "tex_decal_a1", ShaderStage::fragment, ShaderFamily::texture,
     "Texture, cut-out alpha"},
    {Shader::texture_decal_fog_frag, "texture_decal_fog.frag", "tex_decal_fog", ShaderStage::fragment, ShaderFamily::texture,
     "Texture with fog"},
    {Shader::texture_decal_fog_cutout_frag, "texture_decal_fog_cutout.frag", "tex_decal_fog_a1", ShaderStage::fragment, ShaderFamily::texture,
     "Texture with fog, cut-out alpha"},
    {Shader::texture_decal_opaque_frag, "texture_decal_opaque.frag", "tex_decal_afull", ShaderStage::fragment, ShaderFamily::texture,
     "Texture, opaque"},
    {Shader::texture_highlight_frag, "texture_highlight.frag", "tex_col_hl", ShaderStage::fragment, ShaderFamily::texture,
     "Texture × colour + highlight"},
    {Shader::texture_highlight2_frag, "texture_highlight2.frag", "tex_col_hl2", ShaderStage::fragment, ShaderFamily::texture,
     "Texture × colour + highlight, texture alpha"},
    {Shader::texture_highlight2_fog_frag, "texture_highlight2_fog.frag", "tex_col_hl2_fog", ShaderStage::fragment, ShaderFamily::texture,
     "Texture × colour + highlight, texture alpha, fog"},
    {Shader::texture_highlight_fog_frag, "texture_highlight_fog.frag", "tex_col_hl_fog", ShaderStage::fragment, ShaderFamily::texture,
     "Texture × colour + highlight, fog"},
    {Shader::texture_modulate_frag, "texture_modulate.frag", "tex_col_mod2", ShaderStage::fragment, ShaderFamily::texture,
     "Texture × colour"},
    {Shader::texture_modulate_cutout_frag, "texture_modulate_cutout.frag", "tex_col_mod2_a1", ShaderStage::fragment, ShaderFamily::texture,
     "Texture × colour, cut-out alpha"},
    {Shader::texture_modulate_fog_frag, "texture_modulate_fog.frag", "tex_col_mod2_fog", ShaderStage::fragment, ShaderFamily::texture,
     "Texture × colour, fog"},
    {Shader::texture_modulate_fog_cutout_frag, "texture_modulate_fog_cutout.frag", "tex_col_mod2_fog_a1", ShaderStage::fragment, ShaderFamily::texture,
     "Texture × colour, fog, cut-out alpha"},
    {Shader::texture_recolour_frag, "texture_recolour.frag", "tex_col_recol2", ShaderStage::fragment, ShaderFamily::texture,
     "Recoloured texture × colour"},
    {Shader::texture_recolour_cutout_frag, "texture_recolour_cutout.frag", "tex_col_recol2_a1", ShaderStage::fragment, ShaderFamily::texture,
     "Recoloured texture × colour, cut-out alpha"},
    {Shader::texture_recolour_fog_frag, "texture_recolour_fog.frag", "tex_col_recol2_fog", ShaderStage::fragment, ShaderFamily::texture,
     "Recoloured texture × colour, fog"},
    {Shader::texture_recolour_fog_cutout_frag, "texture_recolour_fog_cutout.frag", "tex_col_recol2_fog_a1", ShaderStage::fragment, ShaderFamily::texture,
     "Recoloured texture × colour, fog, cut-out alpha"},
    {Shader::vertex_colour_frag, "vertex_colour.frag", "col", ShaderStage::fragment, ShaderFamily::texture,
     "Colour only"},
    {Shader::colour_grade_blend_frag, "colour_grade_blend.frag", "fx_col_corr2", ShaderStage::fragment, ShaderFamily::screen,
     "Colour grading, blend"},
    {Shader::colour_grade_mask_frag, "colour_grade_mask.frag", "fx_col_corr", ShaderStage::fragment, ShaderFamily::screen,
     "Colour grading, replace"},
    {Shader::depth_fog_table_frag, "depth_fog_table.frag", "fx_grad_fog", ShaderStage::fragment, ShaderFamily::screen,
     "Fog from depth through a table"},
    {Shader::depth_of_field_frag, "depth_of_field.frag", "fx_dof", ShaderStage::fragment, ShaderFamily::screen,
     "Depth of field"},
    {Shader::directional_glow_frag, "directional_glow.frag", "fx_dir_glow", ShaderStage::fragment, ShaderFamily::screen,
     "Directional glow"},
    {Shader::edge_mask_frag, "edge_mask.frag", "fx_edge_aa2", ShaderStage::fragment, ShaderFamily::screen,
     "Edge mask"},
    {Shader::edge_smooth_frag, "edge_smooth.frag", "fx_edge_aa", ShaderStage::fragment, ShaderFamily::screen,
     "Edge smoothing"},
    {Shader::glare_four_taps_frag, "glare_four_taps.frag", "fx_bloom_glare", ShaderStage::fragment, ShaderFamily::screen,
     "Glare from four taps"},
    {Shader::glow_four_taps_frag, "glow_four_taps.frag", "fx_bloom_glow", ShaderStage::fragment, ShaderFamily::screen,
     "Glow from four taps"},
    {Shader::glow_source_frag, "glow_source.frag", "fx_glow_setup", ShaderStage::fragment, ShaderFamily::screen,
     "Glow source"},
    {Shader::object_edge_frag, "object_edge.frag", "fx_edge_det", ShaderStage::fragment, ShaderFamily::screen,
     "Object edges"},
    {Shader::object_edge_directional_frag, "object_edge_directional.frag", "fx_edge_det2", ShaderStage::fragment, ShaderFamily::screen,
     "Object edges, per direction"},
    {Shader::procedural_fog_frag, "procedural_fog.frag", "fx_proc_fog_gen", ShaderStage::fragment, ShaderFamily::screen,
     "Procedural fog"},
    {Shader::star_glare_frag, "star_glare.frag", "fx_star_glare", ShaderStage::fragment, ShaderFamily::screen,
     "Star glare along a direction"},
    {Shader::copy_colour_frag, "copy_colour.frag", "dx_colour_copy", ShaderStage::fragment, ShaderFamily::copy,
     "Copy"},
    {Shader::copy_colour_and_depth_frag, "copy_colour_and_depth.frag", "dx_colour_and_z_copy", ShaderStage::fragment, ShaderFamily::copy,
     "Copy colour and depth"},
    {Shader::copy_depth_frag, "copy_depth.frag", "dx_z_copy", ShaderStage::fragment, ShaderFamily::copy,
     "Copy depth"},
    {Shader::movie_frame_frag, "movie_frame.frag", "video_frame", ShaderStage::fragment, ShaderFamily::movie,
     "Movie frame"},
}};

}  // namespace

std::span<const ShaderInfo> shaders() noexcept { return kShaders; }

const ShaderInfo* info(Shader shader) noexcept {
    const auto i = static_cast<std::size_t>(shader);
    return i < kShaders.size() ? &kShaders[i] : nullptr;
}

Shader find(std::string_view name) noexcept {
    Shader by_stem = Shader::none;
    int stems = 0;
    for (const auto& s : kShaders) {
        const std::string_view file = s.name;
        if (name == file || name == s.dmc3_name) return s.id;
        if (file.substr(0, file.rfind('.')) == name) {
            by_stem = s.id;
            ++stems;
        }
    }
    return stems == 1 ? by_stem : Shader::none;
}

std::string_view source(Shader shader) noexcept {
    const auto* entry = info(shader);
    if (entry == nullptr) return {};
    for (const auto& e : detail::embedded_sources()) {
        if (std::strcmp(e.file, entry->name) == 0) return e.text;
    }
    return {};
}

std::string_view family_name(ShaderFamily family) noexcept {
    switch (family) {
    case ShaderFamily::model: return "model";
    case ShaderFamily::effect_model: return "effect model";
    case ShaderFamily::stage: return "stage";
    case ShaderFamily::shadow: return "shadow";
    case ShaderFamily::particle: return "particle";
    case ShaderFamily::flat: return "flat";
    case ShaderFamily::texture: return "texture";
    case ShaderFamily::screen: return "screen";
    case ShaderFamily::copy: return "copy";
    case ShaderFamily::movie: return "movie";
    }
    return "?";
}

}  // namespace dmcresource::dmc3
