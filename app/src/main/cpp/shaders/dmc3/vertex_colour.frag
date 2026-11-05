#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Colour only: untextured geometry coloured by the vertex colour.

in vec4 v_colour;

uniform int u_alpha_test;
uniform int u_alpha_func;
uniform float u_alpha_ref;

layout(location = 0) out vec4 o_colour;

// Returns true when alpha passes the alpha test function.
bool alpha_passes(float a) {
    if (u_alpha_func == 1) return false;
    if (u_alpha_func == 2) return a < u_alpha_ref;
    if (u_alpha_func == 3) return a == u_alpha_ref;
    if (u_alpha_func == 4) return a <= u_alpha_ref;
    if (u_alpha_func == 5) return a > u_alpha_ref;
    if (u_alpha_func == 6) return a != u_alpha_ref;
    if (u_alpha_func == 7) return a >= u_alpha_ref;
    return true;
}

void main() {
    vec3 rgb = v_colour.rgb;
    float alpha = v_colour.a * 2.0;

    // Alpha test on the final alpha.
    if (u_alpha_test != 0 && !alpha_passes(alpha)) {
        discard;
    }

    o_colour = vec4(rgb, alpha);
}
