#version 300 es
// Recoloured texture x colour, cut-out alpha, with alpha test.
precision highp float;
precision highp int;
precision highp sampler2D;

in vec2 v_uv;
in vec4 v_colour;

uniform sampler2D u_texture;
uniform vec4 u_recolour_low;
uniform vec4 u_recolour_high;

uniform int u_alpha_test;
uniform int u_alpha_func;
uniform float u_alpha_ref;

layout(location = 0) out vec4 o_colour;

// Returns true when alpha passes the comparison selected by func.
bool alpha_passes(int func, float alpha, float ref)
{
    if (func == 1) return false;
    if (func == 2) return alpha < ref;
    if (func == 3) return alpha == ref;
    if (func == 4) return alpha <= ref;
    if (func == 5) return alpha > ref;
    if (func == 6) return alpha != ref;
    if (func == 7) return alpha >= ref;
    return true;
}

void main()
{
    vec4 t = texture(u_texture, v_uv);

    // Recolour by brightness: blend between the low and high rates.
    float b = (t.r + t.g + t.b) * 0.66666666;
    vec4 rate = mix(u_recolour_low, u_recolour_high, b);
    vec3 r = mix(t.rgb, rate.rgb, rate.a);

    // Tint with the vertex colour (x2 rule).
    vec3 rgb = r * v_colour.rgb * 2.0;

    // Cut-out alpha from the texture's own alpha.
    float alpha = (t.a >= 0.95) ? 1.0 : 0.0;

    if (u_alpha_test != 0 && !alpha_passes(u_alpha_func, alpha, u_alpha_ref)) {
        discard;
    }

    o_colour = vec4(rgb, alpha);
}
