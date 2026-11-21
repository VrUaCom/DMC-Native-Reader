// The NR shader library (nr_shaders.h) and Core's choice of shaders per
// batch (nr_draw): every shader has its name, the game's name and its
// source; docs/SHADERS.md lists the same table; models and vertex-coloured
// stages are drawn with the game's shaders, the rest keeps the Reader's.
#include <cassert>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "dmcresource/nr_shaders.h"
#include "dmcresource/view_gpu.h"

using namespace dmcresource;
using nr::AlphaFunc;
using nr::Shader;

namespace {

void library() {
    const auto all = nr::shaders();
    assert(all.size() == 67U);
    std::set<std::string> names;
    std::set<std::string> game_names;
    int vertex = 0;
    for (std::size_t i = 0U; i < all.size(); ++i) {
        const auto& s = all[i];
        assert(static_cast<std::size_t>(s.id) == i);
        assert(nr::info(s.id) == &s);
        assert(names.insert(s.name).second);
        assert(game_names.insert(s.game_name).second);
        const std::string name = s.name;
        const bool is_vertex = name.ends_with(".vert");
        assert(is_vertex || name.ends_with(".frag"));
        assert((s.stage == nr::ShaderStage::vertex) == is_vertex);
        vertex += is_vertex ? 1 : 0;
        const auto text = nr::source(s.id);
        assert(text.starts_with("#version 300 es"));
        assert(text.find(is_vertex ? "gl_Position" : "void main") != std::string_view::npos);
        assert(nr::find(s.name) == s.id);
        assert(nr::find(s.game_name) == s.id);
        assert(s.role != nullptr && s.role[0] != '\0');
    }
    assert(vertex == 29);
    assert(nr::info(Shader::none) == nullptr);
    assert(nr::source(Shader::none).empty());
    assert(nr::find("dmc3_stg_fog") == Shader::stage_lit_fog_vert);
    assert(nr::find("texture_modulate") == Shader::texture_modulate_frag);
    assert(nr::find("movie_frame") == Shader::none);  // both stages have it
    assert(nr::find("no_such_shader") == Shader::none);
    assert(nr::family_name(nr::info(Shader::model_lit_vert)->family) == "model");
}

// docs/SHADERS.md: one row per shader, our name then the game's.
void map_document() {
    std::ifstream file(DMC_NATIVE_READER_REPO_DIR "/docs/SHADERS.md");
    assert(file);
    std::stringstream text;
    text << file.rdbuf();
    const auto doc = text.str();
    for (const auto& s : nr::shaders()) {
        const auto row = std::string("| `") + s.name + "` | " + s.game_name + " |";
        assert(doc.find(row) != std::string::npos);
    }
}

GpuViewFrame frame_with(bool enabled, bool fog) {
    GpuViewFrame f;
    f.nr.enabled = enabled;
    f.nr.fog = fog;
    f.nr.fog_colour = {0.25F, 0.5F, 0.75F};
    f.nr.model_fog = 0.6F;
    f.nr.light_direction = {0.0F, 0.6F, -0.8F};
    return f;
}

void choices() {
    static const ImagePreview texture{};
    GpuBatch model;
    model.texture = &texture;
    model.kind = GpuBatchKind::model;
    model.light_base = 0.72F;
    model.light_gain = 0.42F;

    // Off: the Reader's shaders.
    assert(!nr_draw(model, GpuPass::model, frame_with(false, false)).drawn());

    // A character: dmc3_mod + tex_col_mod2, the camera light as two opposite
    // lights and a constant term (x 128: model_lit divides by it).
    auto d = nr_draw(model, GpuPass::model, frame_with(true, false));
    assert(d.vertex == Shader::model_lit_vert && d.fragment == Shader::texture_modulate_frag);
    assert(d.alpha_func == AlphaFunc::greater_equal);
    assert(d.light_directions[4] == 0.6F && d.light_directions[5] == -0.6F && d.light_directions[15] == 1.0F);
    assert(std::fabs(d.light_colours[0] - 0.42F * 128.0F) < 1.0e-3F);
    assert(std::fabs(d.light_colours[12] - 0.72F * 128.0F) < 1.0e-3F);
    // The object's part alpha (game_material.h): 128/255, or 1 above 0x80.
    assert(d.part_colour == 0.5F && d.part_alpha == 128.0F / 255.0F);
    auto overridden = model;
    overridden.part_alpha = 1.0F;
    assert(nr_draw(overridden, GpuPass::model, frame_with(true, false)).part_alpha == 1.0F);

    // The stage's lights (stage_lights_frame): one row and colour column
    // per light, the constant term; coloured effect models keep theirs.
    auto lit = frame_with(true, false);
    lit.nr.model_rig = true;
    lit.nr.model_light_count = 2;
    lit.nr.model_light_ambient = {220.0F, 210.0F, 200.0F};
    lit.nr.model_light_directions = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F};
    lit.nr.model_light_colours = {240.0F, 144.0F, 104.0F, 20.0F, 45.0F, 50.0F, 0.0F, 0.0F, 0.0F};
    d = nr_draw(model, GpuPass::model, lit);
    assert(d.light_directions[0] == 1.0F && d.light_directions[9] == -1.0F && d.light_directions[15] == 1.0F);
    assert(d.light_directions[4] == 0.0F && d.light_directions[1] == 0.0F);
    assert(d.light_colours[0] == 240.0F && d.light_colours[5] == 45.0F && d.light_colours[8] == 0.0F);
    assert(d.light_colours[12] == 220.0F && d.light_colours[14] == 200.0F);
    // Coloured (effect) models: model_effect_lit does not divide by 128.
    auto coloured_lit = model;
    coloured_lit.colored = true;
    d = nr_draw(coloured_lit, GpuPass::model, lit);
    assert(std::fabs(d.light_colours[0] - 240.0F / 128.0F) < 1.0e-5F);
    assert(std::fabs(d.light_colours[12] - 220.0F / 128.0F) < 1.0e-5F);

    // Punch-through objects (alpha control above 0x80, no blend mode): the
    // game's A1 pixel shaders, with and without fog.
    auto cut = model;
    cut.punch_through = true;
    assert(nr_draw(cut, GpuPass::model, frame_with(true, false)).fragment == Shader::texture_modulate_cutout_frag);
    assert(nr_draw(cut, GpuPass::model, frame_with(true, true)).fragment == Shader::texture_modulate_fog_cutout_frag);

    // Enhanced lighting: a model lit only by its vertex colours gets the
    // camera light (key, weak back, constant); the game's choice otherwise.
    auto baked = model;
    baked.colored = true;
    baked.light_base = 1.0F;
    baked.light_gain = 0.0F;
    d = nr_draw(baked, GpuPass::model, frame_with(true, false));
    assert(d.light_colours[0] == 0.0F && d.light_colours[12] == 1.0F);
    auto enhanced = frame_with(true, false);
    enhanced.options.shading = 1;
    d = nr_draw(baked, GpuPass::model, enhanced);
    assert(d.vertex == Shader::effect_model_lit_vert);
    assert(d.light_directions[4] == 0.6F && d.light_directions[5] == -0.6F && d.light_directions[15] == 1.0F);
    assert(d.light_colours[0] > d.light_colours[4] && d.light_colours[4] > 0.0F && d.light_colours[12] > 0.0F);
    auto baked_added = baked;
    baked_added.kind = GpuBatchKind::model_additive;
    assert(nr_draw(baked_added, GpuPass::model, enhanced).light_colours[0] == 0.0F);
    assert(nr_draw(baked, GpuPass::model, lit).light_colours[0] == 240.0F / 128.0F);

    // With fog: the fog variant, the frame's colour, the model's amount.
    d = nr_draw(model, GpuPass::model, frame_with(true, true));
    assert(d.fragment == Shader::texture_modulate_fog_frag);
    assert(d.fog_colour[1] == 0.5F && d.fog_amount == 0.6F);

    // Added surfaces fade to black in the fog.
    auto added = model;
    added.kind = GpuBatchKind::model_additive;
    assert(nr_draw(added, GpuPass::model, frame_with(true, true)).fog_colour[1] == 0.0F);

    // Vertex colours: the effect model shader (no division by 128).
    auto coloured = model;
    coloured.colored = true;
    coloured.light_base = 1.0F;
    coloured.light_gain = 0.0F;
    d = nr_draw(coloured, GpuPass::model, frame_with(true, false));
    assert(d.vertex == Shader::effect_model_lit_vert);
    assert(d.light_colours[12] == 1.0F && d.light_colours[0] == 0.0F);

    // Untextured: the Reader's grey preview.
    auto bare = model;
    bare.texture = nullptr;
    assert(!nr_draw(bare, GpuPass::model, frame_with(true, false)).drawn());

    // A stage lit by its vertex colours: dmc3_stg, alpha tests per pass.
    GpuBatch room;
    room.texture = &texture;
    room.kind = GpuBatchKind::room;
    room.colored = true;
    d = nr_draw(room, GpuPass::room_opaque, frame_with(true, true));
    assert(d.vertex == Shader::stage_lit_fog_vert && d.fragment == Shader::texture_modulate_fog_frag);
    assert(d.alpha_func == AlphaFunc::greater_equal && std::fabs(d.alpha_ref - 31.5F / 255.0F) < 1.0e-6F);
    assert(std::fabs(d.ambient[0] - 255.0F / 256.0F) < 1.0e-6F);
    room.translucent = true;
    assert(std::fabs(nr_draw(room, GpuPass::room_opaque, frame_with(true, false)).alpha_ref - 239.5F / 255.0F) <
           1.0e-6F);
    d = nr_draw(room, GpuPass::room_soft, frame_with(true, false));
    assert(d.vertex == Shader::stage_lit_vert && d.alpha_func == AlphaFunc::less);
    assert(d.light_colour[0] == 0.0F && d.light_falloff[0] == 0.0F);
    auto lights = frame_with(true, false);
    lights.nr.stage_light_count = 1;
    lights.nr.stage_light_x = {450.0F, 0.0F, 0.0F, 0.0F};
    lights.nr.stage_light_falloff = {1.0e-6F, 0.0F, 0.0F, 0.0F};
    lights.nr.stage_light_colour[0] = 0.5F;
    d = nr_draw(room, GpuPass::room_soft, lights);
    assert(d.light_x[0] == 450.0F && d.light_falloff[0] == 1.0e-6F && d.light_colour[0] == 0.5F);
    // The table's ambient replaces the neutral one only with the stage's rig.
    assert(std::fabs(d.ambient[1] - 255.0F / 256.0F) < 1.0e-6F);
    lights.nr.stage_rig = true;
    lights.nr.stage_ambient = {220.0F / 255.0F, 210.0F / 255.0F, 200.0F / 255.0F};
    d = nr_draw(room, GpuPass::room_soft, lights);
    assert(d.ambient[0] == 220.0F / 255.0F && d.ambient[2] == 200.0F / 255.0F);

    // Light shafts and stages the Reader lights itself keep its shaders.
    auto shaft = room;
    shaft.kind = GpuBatchKind::room_additive;
    assert(!nr_draw(shaft, GpuPass::room_layered, frame_with(true, false)).drawn());
    auto faced = room;
    faced.colored = false;
    faced.light_base = 0.7F;
    faced.light_gain = 0.3F;
    assert(!nr_draw(faced, GpuPass::room_opaque, frame_with(true, false)).drawn());
}

}  // namespace

int main() {
    library();
    map_document();
    choices();
    return 0;
}
