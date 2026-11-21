#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Picks out the glowing parts of a picture using two one-row tables.

in vec2 v_uv;

uniform sampler2D u_texture;
uniform sampler2D u_glow_scale; // table, looked up by the pixel's green
uniform sampler2D u_object_ids; // table, looked up by the pixel's alpha

layout(location = 0) out vec4 o_colour;

void main() {
    vec4 p = texture(u_texture, v_uv);

    // Scale by the glow table entry for the pixel's green.
    float a = texture(u_glow_scale, vec2(p.g, 0.5)).a;
    vec3 rgb = clamp(p.rgb * a * 2.0, 0.0, 1.0);

    // Scale by the object-id table entry for the pixel's alpha.
    float b = texture(u_object_ids, vec2(p.a, 0.5)).a;
    rgb = clamp(rgb * b * 2.0, 0.0, 1.0);

    o_colour = vec4(rgb, 1.0);
}
