#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

// Colour grading, blend: masked pixels move toward a colour table entry
// chosen by their green.

in vec2 v_uv;

uniform sampler2D u_texture;
uniform sampler2D u_object_ids; // table, looked up by the pixel's alpha
uniform sampler2D u_grade;      // table, looked up by the pixel's green

layout(location = 0) out vec4 o_colour;

void main() {
    vec4 p = texture(u_texture, v_uv);
    vec3 rgb = p.rgb;

    // Mask: the object id table entry for this pixel's alpha.
    vec4 mask = texture(u_object_ids, vec2(p.a, 0.5));
    if (mask.a > 0.0) {
        // Blend toward the grade table entry for this pixel's green.
        vec4 g = texture(u_grade, vec2(p.g, 0.5));
        rgb = p.rgb + (g.rgb - p.rgb) * (g.a * 2.0);
    }

    o_colour = vec4(rgb, 1.0);
}
