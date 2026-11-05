#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Texture with fog.

in vec2 v_uv;
in vec4 v_fog; // rgb: fog colour; a: fog amount (1 = no fog, 0 = all fog)

uniform sampler2D u_texture;

layout(location = 0) out vec4 o_colour;

void main() {
    vec4 t = texture(u_texture, v_uv);
    // Fog: an amount of 1 keeps the texel colour, 0 gives the fog colour.
    vec3 rgb = mix(v_fog.rgb, t.rgb, v_fog.a);
    o_colour = vec4(rgb, t.a);
}
