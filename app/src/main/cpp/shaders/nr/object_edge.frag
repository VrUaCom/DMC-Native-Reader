#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Marks where one object meets another: the object ids of this pixel and
// its neighbour half a pixel away differ.

in vec2 v_uv;

uniform sampler2D u_texture;
uniform sampler2D u_object_ids; // table, one row high, looked up by alpha
uniform vec2 u_texel;           // 1 / picture size

layout(location = 0) out vec4 o_colour;

void main() {
    vec2 h = u_texel * 0.5;

    // This pixel and its neighbour half a pixel away.
    vec4 a = texture(u_texture, v_uv);
    vec4 b = texture(u_texture, v_uv + h);

    // Object ids from the table, indexed by each pixel's alpha.
    vec4 ia = texture(u_object_ids, vec2(a.a, 0.5));
    vec4 ib = texture(u_object_ids, vec2(b.a, 0.5));
    vec4 diff = abs(ia - ib);
    float t = diff.r + diff.b;

    o_colour = vec4(0.0, 0.0, 0.0, clamp(t * 255.0, 0.0, 1.0));
}
