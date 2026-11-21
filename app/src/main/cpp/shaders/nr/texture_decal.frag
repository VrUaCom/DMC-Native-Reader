#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Surface showing its texture unchanged, with an optional alpha test.

in vec2 v_uv;

uniform sampler2D u_texture;

uniform int u_alpha_test;
uniform int u_alpha_func;
uniform float u_alpha_ref;

layout(location = 0) out vec4 o_colour;

// Returns true when alpha passes the comparison selected by func.
bool alpha_test_passes(float alpha, int func, float ref) {
    if (func == 1) return false;
    if (func == 2) return alpha < ref;
    if (func == 3) return alpha == ref;
    if (func == 4) return alpha <= ref;
    if (func == 5) return alpha > ref;
    if (func == 6) return alpha != ref;
    if (func == 7) return alpha >= ref;
    return true;
}

void main() {
    vec4 t = texture(u_texture, v_uv);
    vec4 colour = t;

    // Alpha test on the final alpha.
    if (u_alpha_test != 0 && !alpha_test_passes(colour.a, u_alpha_func, u_alpha_ref)) {
        discard;
    }

    o_colour = colour;
}
