#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Texture recoloured by brightness between a low and a high colour, then tinted.

in vec2 v_uv;
in vec4 v_colour;

uniform sampler2D u_texture;
uniform vec4 u_recolour_low;
uniform vec4 u_recolour_high;

uniform int u_alpha_test;
uniform int u_alpha_func;
uniform float u_alpha_ref;

layout(location = 0) out vec4 o_colour;

// Returns true when alpha passes the comparison selected by u_alpha_func.
bool alpha_test_passes(float alpha)
{
    if (u_alpha_func == 1) return false;
    if (u_alpha_func == 2) return alpha < u_alpha_ref;
    if (u_alpha_func == 3) return alpha == u_alpha_ref;
    if (u_alpha_func == 4) return alpha <= u_alpha_ref;
    if (u_alpha_func == 5) return alpha > u_alpha_ref;
    if (u_alpha_func == 6) return alpha != u_alpha_ref;
    if (u_alpha_func == 7) return alpha >= u_alpha_ref;
    return true;
}

void main()
{
    vec4 t = texture(u_texture, v_uv);

    // Brightness picks a recolour rate between the low and high colours.
    float b = (t.r + t.g + t.b) * 0.66666666;
    vec4 rate = mix(u_recolour_low, u_recolour_high, b);
    vec3 r = mix(t.rgb, rate.rgb, rate.a);

    // Tint with the vertex colour (x2 rule, all four channels).
    vec4 c = vec4(r, t.a) * v_colour * 2.0;

    // Alpha test on the final alpha.
    if (u_alpha_test != 0 && !alpha_test_passes(c.a)) {
        discard;
    }

    o_colour = c;
}
