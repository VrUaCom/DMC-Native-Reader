#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Texture tinted by the vertex colour (x2: a colour of 0.5 leaves the texel unchanged).

in vec2 v_uv;
in vec4 v_colour;

uniform sampler2D u_texture;

uniform int u_alpha_test;
uniform int u_alpha_func;
uniform float u_alpha_ref;

layout(location = 0) out vec4 o_colour;

// Returns true when alpha passes the alpha test for the given function.
bool alpha_passes(float a, int func, float ref)
{
    if (func == 1) return false;
    if (func == 2) return a < ref;
    if (func == 3) return a == ref;
    if (func == 4) return a <= ref;
    if (func == 5) return a > ref;
    if (func == 6) return a != ref;
    if (func == 7) return a >= ref;
    return true;
}

void main()
{
    vec4 t = texture(u_texture, v_uv);
    vec4 c = t * v_colour * 2.0;

    // Alpha test on the final alpha.
    if (u_alpha_test != 0 && !alpha_passes(c.a, u_alpha_func, u_alpha_ref))
        discard;

    o_colour = c;
}
