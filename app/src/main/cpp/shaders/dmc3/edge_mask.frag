#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Edge mask: dark where a pixel differs from its neighbour half a pixel away.

in vec2 v_uv;

uniform sampler2D u_texture;
uniform vec2 u_texel; // 1 / picture size

layout(location = 0) out vec4 o_colour;

void main() {
    vec2 h = u_texel * 0.5;
    vec4 a = texture(u_texture, v_uv);
    vec4 b = texture(u_texture, v_uv + h);
    vec4 diff = abs(a - b);
    float m = 1.0 - clamp(diff.r + diff.g + diff.b, 0.0, 1.0) * 2.0;
    o_colour = vec4(vec3(m), 1.0);
}
