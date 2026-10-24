#version 300 es
// Recoloured texture x colour, fog, cut-out alpha, alpha test.
precision highp float;
precision highp int;
precision highp sampler2D;

in vec2 v_uv;
in vec4 v_colour;
in vec4 v_fog;

uniform sampler2D u_texture;
uniform vec4 u_recolour_low;
uniform vec4 u_recolour_high;

uniform int u_alpha_test;
uniform int u_alpha_func;
uniform float u_alpha_ref;

layout(location = 0) out vec4 o_colour;

// Returns true when alpha passes the comparison against the reference.
bool alpha_passes(float a)
{
    if (u_alpha_func == 1) return false;
    if (u_alpha_func == 2) return a < u_alpha_ref;
    if (u_alpha_func == 3) return a == u_alpha_ref;
    if (u_alpha_func == 4) return a <= u_alpha_ref;
    if (u_alpha_func == 5) return a > u_alpha_ref;
    if (u_alpha_func == 6) return a != u_alpha_ref;
    if (u_alpha_func == 7) return a >= u_alpha_ref;
    return true;
}

void main()
{
    vec4 t = texture(u_texture, v_uv);

    // Recolour: blend towards a rate chosen by texel brightness.
    float b = (t.r + t.g + t.b) * 0.66666666;
    vec4 rate = mix(u_recolour_low, u_recolour_high, b);
    vec3 r = mix(t.rgb, rate.rgb, rate.a);

    // Tint by vertex colour (x2), then fog.
    vec3 rgb = r * v_colour.rgb * 2.0;
    rgb = mix(v_fog.rgb, rgb, clamp(v_fog.a, 0.0, 1.0));

    // Cut-out alpha from the untinted texel.
    float alpha = t.a >= 0.95 ? 1.0 : 0.0;

    if (u_alpha_test != 0 && !alpha_passes(alpha)) {
        discard;
    }

    o_colour = vec4(rgb, alpha);
}
