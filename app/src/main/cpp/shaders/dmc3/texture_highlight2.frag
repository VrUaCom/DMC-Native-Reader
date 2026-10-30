#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Texture x colour (x2) brightened by the colour's alpha; keeps the texture's alpha.

in vec2 v_uv;
in vec4 v_colour;

uniform sampler2D u_texture;

uniform int u_alpha_test;
uniform int u_alpha_func;
uniform float u_alpha_ref;

layout(location = 0) out vec4 o_colour;

// Returns true when alpha passes the comparison selected by u_alpha_func.
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

    // Tint the texel (0.5 leaves it unchanged) and add the colour's alpha as a highlight.
    vec3 rgb = t.rgb * v_colour.rgb * 2.0 + v_colour.a;
    float alpha = t.a;

    // Alpha test on the final alpha.
    if (u_alpha_test != 0 && !alpha_passes(alpha)) {
        discard;
    }

    o_colour = vec4(rgb, alpha);
}
