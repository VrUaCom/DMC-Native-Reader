#version 300 es
// Texture x colour (x2), fog, cut-out alpha, alpha test.
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

// Returns true when alpha passes the comparison selected by u_alpha_func.
bool alpha_test_passes(float a)
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
    vec4 c = t * v_colour * 2.0;

    // Fog: amount 1 keeps the colour, 0 gives the fog colour.
    c.rgb = mix(v_fog.rgb, c.rgb, v_fog.a);

    // Cut-out: alpha is 1 when at least 0.95, else 0.
    c.a = (c.a >= 0.95) ? 1.0 : 0.0;

    // Alpha test on the final alpha.
    if (u_alpha_test != 0 && !alpha_test_passes(c.a)) {
        discard;
    }

    o_colour = c;
}
