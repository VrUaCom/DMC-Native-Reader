# Clean-room shaders: what each one replaces

For the specification side only; the implementers never read this file.

How the shaders were made:

1. The specification side read the HD port's shader sources (debug blocks
   of `dmc3.exe`, see research/dmc3-hd-shaders-exe.md) and wrote
   CONVENTIONS.md and one behaviour specification per shader in `specs/`:
   in its own words, under its own names, with no source text.
2. The extracted sources and the executable were then removed from the
   working machine.
3. One implementer per shader, who had not seen the sources, wrote the
   shader from CONVENTIONS.md and its specification alone, and checked that
   it compiles with OpenGL ES 3.0 (Mesa).

Neither the game's sources nor its executable are in this repository.

| our shader | the HD port's shader it behaves like | stage |
| --- | --- | --- |
| `model_lit.vert` | dmc3_mod | vertex |
| `model_lit_sheen.vert` | dmc3_mod_sp | vertex |
| `model_screen_uv.vert` | dmc3_mod_stx | vertex |
| `effect_model_lit.vert` | dmc3_efm | vertex |
| `effect_model_lit_sheen.vert` | dmc3_efm_sp | vertex |
| `effect_model_screen_uv.vert` | dmc3_efm_stx | vertex |
| `effect_model_vertex_alpha.vert` | dmc3_efm_va | vertex |
| `effect_model_vertex_alpha_sheen.vert` | dmc3_efm_va_sp | vertex |
| `stage_lit.vert` | dmc3_stg | vertex |
| `stage_lit_fog.vert` | dmc3_stg_fog | vertex |
| `shadow_volume.vert` | dmc3_shw | vertex |
| `particle.vert` | dmc3_ptcl | vertex |
| `particle_fog.vert` | dmc3_ptcl_fog | vertex |
| `flat_object_colour.vert` | ocpl | vertex |
| `flat_object_colour_fog.vert` | ocpl_fog | vertex |
| `flat_vertex_colour.vert` | vcpl | vertex |
| `flat_vertex_colour_fog.vert` | vcpl_fog | vertex |
| `flat_textured.vert` | vtpl | vertex |
| `flat_textured_fog.vert` | vtpl_fog | vertex |
| `flat_textured_object_colour.vert` | vtocpl | vertex |
| `flat_textured_object_colour_fog.vert` | vtocpl_fog | vertex |
| `flat_textured_vertex_colour.vert` | vtvcpl | vertex |
| `flat_textured_vertex_colour_fog.vert` | vtvcpl_fog | vertex |
| `screen_pass_one_param.vert` | fx_1x4f_pass | vertex |
| `screen_pass_two_params.vert` | fx_2x4f_pass | vertex |
| `screen_pass_plain.vert` | fx_col_corr_v | vertex |
| `screen_pass_directional.vert` | fx_dir_glow_v | vertex |
| `screen_pass_depth_of_field.vert` | fx_dof_v | vertex |
| `movie_frame.vert` | video_frame_v | vertex |
| `vertex_colour.frag` | col | pixel |
| `copy_colour_and_depth.frag` | dx_colour_and_z_copy | pixel |
| `copy_colour.frag` | dx_colour_copy | pixel |
| `copy_depth.frag` | dx_z_copy | pixel |
| `glare_four_taps.frag` | fx_bloom_glare | pixel |
| `glow_four_taps.frag` | fx_bloom_glow | pixel |
| `colour_grade_mask.frag` | fx_col_corr | pixel |
| `colour_grade_blend.frag` | fx_col_corr2 | pixel |
| `palette_lookup.frag` | fx_decal_clut | pixel |
| `directional_glow.frag` | fx_dir_glow | pixel |
| `depth_of_field.frag` | fx_dof | pixel |
| `edge_smooth.frag` | fx_edge_aa | pixel |
| `edge_mask.frag` | fx_edge_aa2 | pixel |
| `object_edge.frag` | fx_edge_det | pixel |
| `object_edge_directional.frag` | fx_edge_det2 | pixel |
| `glow_source.frag` | fx_glow_setup | pixel |
| `depth_fog_table.frag` | fx_grad_fog | pixel |
| `procedural_fog.frag` | fx_proc_fog_gen | pixel |
| `star_glare.frag` | fx_star_glare | pixel |
| `texture_highlight.frag` | tex_col_hl | pixel |
| `texture_highlight_fog.frag` | tex_col_hl_fog | pixel |
| `texture_highlight2.frag` | tex_col_hl2 | pixel |
| `texture_highlight2_fog.frag` | tex_col_hl2_fog | pixel |
| `texture_modulate.frag` | tex_col_mod2 | pixel |
| `texture_modulate_cutout.frag` | tex_col_mod2_a1 | pixel |
| `texture_modulate_fog.frag` | tex_col_mod2_fog | pixel |
| `texture_modulate_fog_cutout.frag` | tex_col_mod2_fog_a1 | pixel |
| `texture_recolour.frag` | tex_col_recol2 | pixel |
| `texture_recolour_cutout.frag` | tex_col_recol2_a1 | pixel |
| `texture_recolour_fog.frag` | tex_col_recol2_fog | pixel |
| `texture_recolour_fog_cutout.frag` | tex_col_recol2_fog_a1 | pixel |
| `texture_decal.frag` | tex_decal | pixel |
| `texture_decal_cutout.frag` | tex_decal_a1 | pixel |
| `texture_decal_opaque.frag` | tex_decal_afull | pixel |
| `texture_decal_fog.frag` | tex_decal_fog | pixel |
| `texture_decal_fog_cutout.frag` | tex_decal_fog_a1 | pixel |
| `particle_alpha_boost.frag` | tex_ptcl_end | pixel |
| `movie_frame.frag` | video_frame | pixel |
