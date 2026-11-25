#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Texture with cut-out (all or nothing) transparency.

in vec2 v_uv;

uniform sampler2D u_texture;

uniform int u_alpha_test;
uniform int u_alpha_func;
uniform float u_alpha_ref;

layout(location = 0) out vec4 o_colour;

// Alpha test: true when alpha passes the comparison against ref.
bool alphaTestPasses(float alpha, int func, float ref) {
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

    // Cut-out: alpha is 1 when at least 0.95, else 0.
    colour.a = (t.a >= 0.95) ? 1.0 : 0.0;

    // Alpha test on the final alpha.
    if (u_alpha_test != 0 && !alphaTestPasses(colour.a, u_alpha_func, u_alpha_ref)) {
        discard;
    }

    o_colour = colour;
}
