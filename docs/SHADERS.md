# DMC3 shaders: our names and the game's

The Reader's 67 GLSL ES 3.00 shaders in `app/src/main/cpp/shaders/dmc3/`
behave like the shaders of the DMC3 HD port. They were written clean-room
from behaviour specifications (`docs/cleanroom/`), not copied. This page maps
each one to the game's shader it stands for.

In the code, `dmcresource::dmc3` (`include/dmcresource/dmc3_shaders.h`) holds
the same table:
- `dmc3::Shader::model_lit_vert` is `model_lit.vert`, which is `dmc3_mod`.
- `dmc3::find("dmc3_mod")` and `dmc3::find("model_lit.vert")` both give it.
- `dmc3::source()` gives its text; the build embeds the files.

The test `dmc3_shaders` keeps this page and the table in step.

## How the Reader uses them

- When the GPU backend starts, it compiles all 67.
- For every batch, the core's `dmc3_draw()` (`view_renderer.cpp`) picks the
  pair, the alpha test and the light rig. That function is the one place to
  change when a batch should use another shader.
- The backend links a pair the first time a frame needs it and sets the
  uniforms that `docs/cleanroom/CONVENTIONS.md` names.

| Reader draws | Vertex shader | Fragment shader | Notes |
| --- | --- | --- | --- |
| Textured model, no vertex colours | `model_lit.vert` (dmc3_mod) | `texture_modulate[_fog].frag` (tex_col_mod2[_fog]) | The vertices arrive posed, so every vertex follows bone 0, the identity. The light rig is the Reader's camera light: two opposite lights and a constant term. With Stage lights on it is the stage's instead: up to three of its LIG2 lights, the game's top four for the model's centre, plus a constant term (both model shaders). Part alpha: the object's (128/255, or 1 above 0x80; `game_material.h`). Fog is one amount for the model, taken at its centre, as the game does per part. |
| Textured model with vertex colours | `effect_model_lit.vert` (dmc3_efm) | `texture_modulate[_fog].frag` | Alpha is the texel's; the game's shader ignores the vertex alpha. |
| Stage lit by its vertex colours: opaque texels | `stage_lit[_fog].vert` (dmc3_stg[_fog]) | `texture_modulate[_fog].frag` | Alpha test ≥ 31.5/255, or ≥ 239.5/255 on soft-alpha textures. The ambient makes 0x80 equal 1. Point lights only with Stage lights on: the four of the stage configuration's LIG2 table nearest the framing centre (`game_lights.h`). |
| The same: soft texels | `stage_lit[_fog].vert` | `texture_modulate[_fog].frag` | Alpha test < 239.5/255, blended. |
| Added and subtracted models | as above | as above | Fog colour is black, so they fade into the fog. |

Fog uses the game's curve: amount = x + y / distance. It matches the Reader
fog's amounts at its start and its end, and between them it follows the
PS2 curve. The CPU renderer keeps its straight-line fog.

The Reader's own shaders stay in use for:
- untextured models (the grey preview);
- stages without vertex colours, which the Reader lights by facing;
- light shafts, which are weighted by vertex alpha;
- fog whose colour changes with distance;
- the floor, the shadow footprint, effect quads, the fade, overlays and the
  photo camera's development.

To turn the DMC3 shaders off, set Settings → Graphics → Shaders (GPU) to
"Reader's own". The Renderer info shows which shaders the last frame used.

The other shaders are in the library and compile. They wait for the data
they draw:
- the game's lights (LIG2) for the sheen variants;
- the cutscenes' screen effects (ScrEfc) for glow, depth of field, fog
  tables and edges;
- particles, flat geometry, shadow volumes and movie frames, for when the
  Reader draws those the game's way.

## All 67

| Our shader | DMC3 HD shader | Stage | Family | What it draws | The Reader uses it for |
| --- | --- | --- | --- | --- | --- |
| `model_lit.vert` | dmc3_mod | vertex | model | Skinned model, lit | Models without vertex colours (characters): the Reader's camera light as the light rig |
| `model_lit_sheen.vert` | dmc3_mod_sp | vertex | model | Skinned model, lit, with sheen |  |
| `model_screen_uv.vert` | dmc3_mod_stx | vertex | model | Skinned model, screen-space texture (haze / camouflage) |  |
| `effect_model_lit.vert` | dmc3_efm | vertex | effect model | Skinned effect model, lit, with vertex colour | Models with vertex colours (cutscene and effect models) |
| `effect_model_lit_sheen.vert` | dmc3_efm_sp | vertex | effect model | Skinned effect model, lit, with vertex colour and sheen |  |
| `effect_model_screen_uv.vert` | dmc3_efm_stx | vertex | effect model | Skinned effect model, screen-space texture (haze / camouflage) |  |
| `effect_model_vertex_alpha.vert` | dmc3_efm_va | vertex | effect model | Skinned effect model, lit, vertex colour and vertex alpha |  |
| `effect_model_vertex_alpha_sheen.vert` | dmc3_efm_va_sp | vertex | effect model | Skinned effect model, lit, vertex colour and alpha, sheen |  |
| `stage_lit.vert` | dmc3_stg | vertex | stage | Stage, four point lights, no fog | Stages lit by their vertex colours, no fog |
| `stage_lit_fog.vert` | dmc3_stg_fog | vertex | stage | Stage, four point lights, distance fog | Stages lit by their vertex colours, with fog |
| `shadow_volume.vert` | dmc3_shw | vertex | shadow | Shadow volume |  |
| `particle.vert` | dmc3_ptcl | vertex | particle | Particle quad |  |
| `particle_fog.vert` | dmc3_ptcl_fog | vertex | particle | Particle quad with distance fog |  |
| `flat_object_colour.vert` | ocpl | vertex | flat | Flat geometry, one colour |  |
| `flat_object_colour_fog.vert` | ocpl_fog | vertex | flat | Flat geometry, one colour, fog |  |
| `flat_textured.vert` | vtpl | vertex | flat | Flat geometry, textured |  |
| `flat_textured_fog.vert` | vtpl_fog | vertex | flat | Flat geometry, textured, fog |  |
| `flat_textured_object_colour.vert` | vtocpl | vertex | flat | Flat geometry, textured, one colour |  |
| `flat_textured_object_colour_fog.vert` | vtocpl_fog | vertex | flat | Flat geometry, textured, one colour, fog |  |
| `flat_textured_vertex_colour.vert` | vtvcpl | vertex | flat | Flat geometry, textured, vertex colours |  |
| `flat_textured_vertex_colour_fog.vert` | vtvcpl_fog | vertex | flat | Flat geometry, textured, vertex colours, fog |  |
| `flat_vertex_colour.vert` | vcpl | vertex | flat | Flat geometry, vertex colours |  |
| `flat_vertex_colour_fog.vert` | vcpl_fog | vertex | flat | Flat geometry, vertex colours, fog |  |
| `screen_pass_depth_of_field.vert` | fx_dof_v | vertex | screen | Full-screen pass for depth of field |  |
| `screen_pass_directional.vert` | fx_dir_glow_v | vertex | screen | Full-screen pass, two coordinate sets |  |
| `screen_pass_one_param.vert` | fx_1x4f_pass | vertex | screen | Full-screen pass, one parameter |  |
| `screen_pass_plain.vert` | fx_col_corr_v | vertex | screen | Full-screen pass, coordinates only |  |
| `screen_pass_two_params.vert` | fx_2x4f_pass | vertex | screen | Full-screen pass, two parameters |  |
| `movie_frame.vert` | video_frame_v | vertex | movie | Movie frame |  |
| `particle_alpha_boost.frag` | tex_ptcl_end | fragment | particle | Particle texture, alpha raised |  |
| `palette_lookup.frag` | fx_decal_clut | fragment | texture | 8-bit picture through a palette |  |
| `texture_decal.frag` | tex_decal | fragment | texture | Texture as it is |  |
| `texture_decal_cutout.frag` | tex_decal_a1 | fragment | texture | Texture, cut-out alpha |  |
| `texture_decal_fog.frag` | tex_decal_fog | fragment | texture | Texture with fog |  |
| `texture_decal_fog_cutout.frag` | tex_decal_fog_a1 | fragment | texture | Texture with fog, cut-out alpha |  |
| `texture_decal_opaque.frag` | tex_decal_afull | fragment | texture | Texture, opaque |  |
| `texture_highlight.frag` | tex_col_hl | fragment | texture | Texture × colour + highlight |  |
| `texture_highlight2.frag` | tex_col_hl2 | fragment | texture | Texture × colour + highlight, texture alpha |  |
| `texture_highlight2_fog.frag` | tex_col_hl2_fog | fragment | texture | Texture × colour + highlight, texture alpha, fog |  |
| `texture_highlight_fog.frag` | tex_col_hl_fog | fragment | texture | Texture × colour + highlight, fog |  |
| `texture_modulate.frag` | tex_col_mod2 | fragment | texture | Texture × colour | Those models and stages, no fog |
| `texture_modulate_cutout.frag` | tex_col_mod2_a1 | fragment | texture | Texture × colour, cut-out alpha |  |
| `texture_modulate_fog.frag` | tex_col_mod2_fog | fragment | texture | Texture × colour, fog | Those models and stages, with fog |
| `texture_modulate_fog_cutout.frag` | tex_col_mod2_fog_a1 | fragment | texture | Texture × colour, fog, cut-out alpha |  |
| `texture_recolour.frag` | tex_col_recol2 | fragment | texture | Recoloured texture × colour |  |
| `texture_recolour_cutout.frag` | tex_col_recol2_a1 | fragment | texture | Recoloured texture × colour, cut-out alpha |  |
| `texture_recolour_fog.frag` | tex_col_recol2_fog | fragment | texture | Recoloured texture × colour, fog |  |
| `texture_recolour_fog_cutout.frag` | tex_col_recol2_fog_a1 | fragment | texture | Recoloured texture × colour, fog, cut-out alpha |  |
| `vertex_colour.frag` | col | fragment | texture | Colour only |  |
| `colour_grade_blend.frag` | fx_col_corr2 | fragment | screen | Colour grading, blend |  |
| `colour_grade_mask.frag` | fx_col_corr | fragment | screen | Colour grading, replace |  |
| `depth_fog_table.frag` | fx_grad_fog | fragment | screen | Fog from depth through a table |  |
| `depth_of_field.frag` | fx_dof | fragment | screen | Depth of field |  |
| `directional_glow.frag` | fx_dir_glow | fragment | screen | Directional glow |  |
| `edge_mask.frag` | fx_edge_aa2 | fragment | screen | Edge mask |  |
| `edge_smooth.frag` | fx_edge_aa | fragment | screen | Edge smoothing |  |
| `glare_four_taps.frag` | fx_bloom_glare | fragment | screen | Glare from four taps |  |
| `glow_four_taps.frag` | fx_bloom_glow | fragment | screen | Glow from four taps |  |
| `glow_source.frag` | fx_glow_setup | fragment | screen | Glow source |  |
| `object_edge.frag` | fx_edge_det | fragment | screen | Object edges |  |
| `object_edge_directional.frag` | fx_edge_det2 | fragment | screen | Object edges, per direction |  |
| `procedural_fog.frag` | fx_proc_fog_gen | fragment | screen | Procedural fog |  |
| `star_glare.frag` | fx_star_glare | fragment | screen | Star glare along a direction |  |
| `copy_colour.frag` | dx_colour_copy | fragment | copy | Copy |  |
| `copy_colour_and_depth.frag` | dx_colour_and_z_copy | fragment | copy | Copy colour and depth |  |
| `copy_depth.frag` | dx_z_copy | fragment | copy | Copy depth |  |
| `movie_frame.frag` | video_frame | fragment | movie | Movie frame |  |
