#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Texture drawn fully opaque.

in vec2 v_uv;

uniform sampler2D u_texture;

layout(location = 0) out vec4 o_colour;

void main() {
    // Texel colour with alpha forced to 1.
    vec4 t = texture(u_texture, v_uv);
    o_colour = vec4(t.rgb, 1.0);
}
