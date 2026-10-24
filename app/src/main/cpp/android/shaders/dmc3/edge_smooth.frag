#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Edge smoothing: blend each pixel toward its neighbour half a pixel away,
// more strongly the more the two differ.

in vec2 v_uv;

uniform sampler2D u_texture;
uniform vec2 u_texel; // 1 / picture size

layout(location = 0) out vec4 o_colour;

void main() {
    vec2 h = u_texel * 0.5;

    vec4 a = texture(u_texture, v_uv);
    vec4 b = texture(u_texture, v_uv + h);

    // Blend weight from the summed rgb difference.
    vec4 diff = abs(a - b);
    float k = clamp((diff.r + diff.g + diff.b) * 4.0 + 0.2, 0.0, 1.0);

    o_colour = vec4(mix(a.rgb, b.rgb, k), 1.0);
}
