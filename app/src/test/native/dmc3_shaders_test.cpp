// The DMC3 shader library (dmc3_shaders.h) and Core's choice of shaders per
// batch (dmc3_draw): every shader has its name, the game's name and its
// source; docs/SHADERS.md lists the same table; models and vertex-coloured
// stages are drawn with the game's shaders, the rest keeps the Reader's.
#include <cassert>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "dmcresource/dmc3_shaders.h"
#include "dmcresource/view_gpu.h"

using namespace dmcresource;
using dmc3::AlphaFunc;
using dmc3::Shader;

namespace {

void library() {
    const auto all = dmc3::shaders();
    assert(all.size() == 67U);
    std::set<std::string> names;
    std::set<std::string> game_names;
    int vertex = 0;
    for (std::size_t i = 0U; i < all.size(); ++i) {
        const auto& s = all[i];
        assert(static_cast<std::size_t>(s.id) == i);
        assert(dmc3::info(s.id) == &s);
        assert(names.insert(s.name).second);
        assert(game_names.insert(s.dmc3_name).second);
        const std::string name = s.name;
        const bool is_vertex = name.ends_with(".vert");
        assert(is_vertex || name.ends_with(".frag"));
        assert((s.stage == dmc3::ShaderStage::vertex) == is_vertex);
        vertex += is_vertex ? 1 : 0;
        const auto text = dmc3::source(s.id);
        assert(text.starts_with("#version 300 es"));
        assert(text.find(is_vertex ? "gl_Position" : "void main") != std::string_view::npos);
        assert(dmc3::find(s.name) == s.id);
        assert(dmc3::find(s.dmc3_name) == s.id);
        assert(s.role != nullptr && s.role[0] != '\0');
    }
    assert(vertex == 29);
    assert(dmc3::info(Shader::none) == nullptr);
    assert(dmc3::source(Shader::none).empty());
    assert(dmc3::find("dmc3_stg_fog") == Shader::stage_lit_fog_vert);
    assert(dmc3::find("texture_modulate") == Shader::texture_modulate_frag);
    assert(dmc3::find("movie_frame") == Shader::none);  // both stages have it
    assert(dmc3::find("no_such_shader") == Shader::none);
    assert(dmc3::family_name(dmc3::info(Shader::model_lit_vert)->family) == "model");
}

// docs/SHADERS.md: one row per shader, our name then the game's.
void map_document() {
    std::ifstream file(DMC_NATIVE_READER_REPO_DIR "/docs/SHADERS.md");
    assert(file);
    std::stringstream text;
    text << file.rdbuf();
    const auto doc = text.str();
    for (const auto& s : dmc3::shaders()) {
        const auto row = std::string("| `") + s.name + "` | " + s.dmc3_name + " |";
        assert(doc.find(row) != std::string::npos);
    }
}

GpuViewFrame frame_with(bool enabled, bool fog) {
    GpuViewFrame f;
    f.dmc3.enabled = enabled;
    f.dmc3.fog = fog;
    f.dmc3.fog_colour = {0.25F, 0.5F, 0.75F};
    f.dmc3.model_fog = 0.6F;
    f.dmc3.light_direction = {0.0F, 0.6F, -0.8F};
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
    assert(!dmc3_draw(model, GpuPass::model, frame_with(false, false)).drawn());

    // A character: dmc3_mod + tex_col_mod2, the camera light as two opposite
    // lights and a constant term (x 128: model_lit divides by it).
    auto d = dmc3_draw(model, GpuPass::model, frame_with(true, false));
    assert(d.vertex == Shader::model_lit_vert && d.fragment == Shader::texture_modulate_frag);
    assert(d.alpha_func == AlphaFunc::greater_equal);
    assert(d.light_directions[4] == 0.6F && d.light_directions[5] == -0.6F && d.light_directions[15] == 1.0F);
    assert(std::fabs(d.light_colours[0] - 0.42F * 128.0F) < 1.0e-3F);
    assert(std::fabs(d.light_colours[12] - 0.72F * 128.0F) < 1.0e-3F);
    // The object's part alpha (game_material.h): 128/255, or 1 above 0x80.
    assert(d.part_colour == 0.5F && d.part_alpha == 128.0F / 255.0F);
    auto overridden = model;
    overridden.part_alpha = 1.0F;
    assert(dmc3_draw(overridden, GpuPass::model, frame_with(true, false)).part_alpha == 1.0F);

    // The stage's lights (stage_lights_frame): one row and colour column
    // per light, the constant term; coloured effect models keep theirs.
    auto lit = frame_with(true, false);
    lit.dmc3.model_light_count = 2;
    lit.dmc3.model_light_directions = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F};
    lit.dmc3.model_light_colours = {240.0F, 144.0F, 104.0F, 20.0F, 45.0F, 50.0F, 0.0F, 0.0F, 0.0F};
    d = dmc3_draw(model, GpuPass::model, lit);
    assert(d.light_directions[0] == 1.0F && d.light_directions[9] == -1.0F && d.light_directions[15] == 1.0F);
    assert(d.light_directions[4] == 0.0F && d.light_directions[1] == 0.0F);
    assert(d.light_colours[0] == 240.0F && d.light_colours[5] == 45.0F && d.light_colours[8] == 0.0F);
    assert(std::fabs(d.light_colours[12] - 0.45F * 128.0F) < 1.0e-3F);

    // With fog: the fog variant, the frame's colour, the model's amount.
    d = dmc3_draw(model, GpuPass::model, frame_with(true, true));
    assert(d.fragment == Shader::texture_modulate_fog_frag);
    assert(d.fog_colour[1] == 0.5F && d.fog_amount == 0.6F);

    // Added surfaces fade to black in the fog.
    auto added = model;
    added.kind = GpuBatchKind::model_additive;
    assert(dmc3_draw(added, GpuPass::model, frame_with(true, true)).fog_colour[1] == 0.0F);

    // Vertex colours: the effect model shader (no division by 128).
    auto coloured = model;
    coloured.colored = true;
    coloured.light_base = 1.0F;
    coloured.light_gain = 0.0F;
    d = dmc3_draw(coloured, GpuPass::model, frame_with(true, false));
    assert(d.vertex == Shader::effect_model_lit_vert);
    assert(d.light_colours[12] == 1.0F && d.light_colours[0] == 0.0F);

    // Untextured: the Reader's grey preview.
    auto bare = model;
    bare.texture = nullptr;
    assert(!dmc3_draw(bare, GpuPass::model, frame_with(true, false)).drawn());

    // A stage lit by its vertex colours: dmc3_stg, alpha tests per pass.
    GpuBatch room;
    room.texture = &texture;
    room.kind = GpuBatchKind::room;
    room.colored = true;
    d = dmc3_draw(room, GpuPass::room_opaque, frame_with(true, true));
    assert(d.vertex == Shader::stage_lit_fog_vert && d.fragment == Shader::texture_modulate_fog_frag);
    assert(d.alpha_func == AlphaFunc::greater_equal && std::fabs(d.alpha_ref - 31.5F / 255.0F) < 1.0e-6F);
    assert(std::fabs(d.ambient - 255.0F / 256.0F) < 1.0e-6F);
    room.translucent = true;
    assert(std::fabs(dmc3_draw(room, GpuPass::room_opaque, frame_with(true, false)).alpha_ref - 239.5F / 255.0F) <
           1.0e-6F);
    d = dmc3_draw(room, GpuPass::room_soft, frame_with(true, false));
    assert(d.vertex == Shader::stage_lit_vert && d.alpha_func == AlphaFunc::less);
    assert(d.light_colour[0] == 0.0F && d.light_falloff[0] == 0.0F);
    auto lights = frame_with(true, false);
    lights.dmc3.stage_light_count = 1;
    lights.dmc3.stage_light_x = {450.0F, 0.0F, 0.0F, 0.0F};
    lights.dmc3.stage_light_falloff = {1.0e-6F, 0.0F, 0.0F, 0.0F};
    lights.dmc3.stage_light_colour[0] = 0.5F;
    d = dmc3_draw(room, GpuPass::room_soft, lights);
    assert(d.light_x[0] == 450.0F && d.light_falloff[0] == 1.0e-6F && d.light_colour[0] == 0.5F);

    // Light shafts and stages the Reader lights itself keep its shaders.
    auto shaft = room;
    shaft.kind = GpuBatchKind::room_additive;
    assert(!dmc3_draw(shaft, GpuPass::room_layered, frame_with(true, false)).drawn());
    auto faced = room;
    faced.colored = false;
    faced.light_base = 0.7F;
    faced.light_gain = 0.3F;
    assert(!dmc3_draw(faced, GpuPass::room_opaque, frame_with(true, false)).drawn());
}

}  // namespace

int main() {
    library();
    map_document();
    choices();
    return 0;
}
