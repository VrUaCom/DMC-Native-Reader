#version 300 es
// Texture x colour x2 plus highlight (colour alpha), texture alpha, fog, alpha test.
precision highp float;
precision highp int;
precision highp sampler2D;

in vec2 v_uv;
in vec4 v_colour;
in vec4 v_fog;

uniform sampler2D u_texture;

uniform int u_alpha_test;
uniform int u_alpha_func;
uniform float u_alpha_ref;

layout(location = 0) out vec4 o_colour;

// Returns true when alpha passes the comparison selected by func.
bool alpha_passes(float alpha, int func, float ref)
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

    // Modulate by vertex colour x2, add the highlight from the colour's alpha.
    vec3 rgb = t.rgb * v_colour.rgb * 2.0 + v_colour.a;
    float alpha = t.a;

    // Fog: amount 1 keeps the colour, 0 gives the fog colour.
    rgb = mix(v_fog.rgb, rgb, v_fog.a);

    // Alpha test on the final alpha.
    if (u_alpha_test != 0 && !alpha_passes(alpha, u_alpha_func, u_alpha_ref)) {
        discard;
    }

    o_colour = vec4(rgb, alpha);
}
