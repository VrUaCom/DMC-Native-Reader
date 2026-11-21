#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// 8-bit (indexed) picture shown through its palette.

in vec2 v_uv;

uniform sampler2D u_texture; // palette indices in red
uniform sampler2D u_palette; // palette table, one row high

layout(location = 0) out vec4 o_colour;

void main() {
    // Read the index, then take the palette entry at that index.
    float i = texture(u_texture, v_uv).r;
    o_colour = texture(u_palette, vec2(i, 0.5));
}
