#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Tinted texture (x2 rule) brightened by the colour's alpha.

in vec2 v_uv;
in vec4 v_colour;

uniform sampler2D u_texture;

uniform int u_alpha_test;
uniform int u_alpha_func;
uniform float u_alpha_ref;

layout(location = 0) out vec4 o_colour;

// Returns true when alpha passes the configured comparison.
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

    // Texture x colour x 2, plus the colour's alpha added to every channel.
    vec4 c = t * v_colour * 2.0 + vec4(v_colour.a);

    // Alpha test on the final alpha.
    if (u_alpha_test != 0 && !alpha_passes(c.a)) {
        discard;
    }

    o_colour = c;
}
